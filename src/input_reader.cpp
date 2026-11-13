#include "input_reader.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <thread>

#include "config.h"

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace Utils {
namespace Input {
namespace {

// Decode one UTF-8 sequence starting at data[pos]. Returns the scalar
// value (U+FFFD on any invalid form) and advances pos past consumed bytes,
// or 0 and no advance when more bytes are needed for a complete sequence.
char32_t decode_one(const std::string &data, size_t &pos) {
  auto byte = [&](size_t i) -> unsigned char {
    return static_cast<unsigned char>(data[i]);
  };
  unsigned char first = byte(pos);
  size_t need = 0;
  char32_t value = 0;
  char32_t low = 0;
  if (first < 0x80) {
    pos += 1;
    return first;
  } else if ((first & 0xE0) == 0xC0) {
    need = 1;
    value = first & 0x1F;
    low = 0x80;
  } else if ((first & 0xF0) == 0xE0) {
    need = 2;
    value = first & 0x0F;
    low = 0x800;
  } else if ((first & 0xF8) == 0xF0) {
    need = 3;
    value = first & 0x07;
    low = 0x10000;
  } else {
    pos += 1;
    return 0xFFFD;
  }
  if (pos + need >= data.size()) {
    return 0; // incomplete: caller holds bytes for more input.
  }
  for (size_t i = 1; i <= need; ++i) {
    unsigned char c = byte(pos + i);
    if ((c & 0xC0) != 0x80) {
      pos += 1;
      return 0xFFFD;
    }
    value = (value << 6) | (c & 0x3F);
  }
  pos += 1 + need;
  if (value < low || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    return 0xFFFD; // overlong, out of range, or surrogate.
  }
  return value;
}

void append_utf8(std::string *out, char32_t value) {
  if (value < 0x80) {
    out->push_back(static_cast<char>(value));
  } else if (value < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (value >> 6)));
    out->push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else if (value < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (value >> 12)));
    out->push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (value >> 18)));
    out->push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (value & 0x3F)));
  }
}

// True for a CSI final byte: parameter/intermediate bytes precede it.
bool is_csi_final(unsigned char c) { return c >= 0x40 && c <= 0x7E; }

// History stores submitted lines. Empty input is noise, and a line that
// still contains ESC is a raw escape sequence (a paste or a scripted
// line), never something the user meant to recall.
bool is_eligible_history_line(const std::string &line) {
  if (line.empty()) {
    return false;
  }
  return line.find('\x1b') == std::string::npos;
}

} // namespace

void History::add(const std::string &line) {
  if (!is_eligible_history_line(line)) {
    return;
  }
  entries_.push_back(line);
  if (entries_.size() > k_max_entries) {
    entries_.erase(entries_.begin());
  }
}

size_t History::size() const { return entries_.size(); }

const std::string &History::at(size_t index) const { return entries_[index]; }

void History::clear() { entries_.clear(); }

ModalEditor::ModalEditor() = default;

ModalEditor::ModalEditor(History *history, bool allow_normal)
    : history_(history), allow_normal_(allow_normal) {}

EditMode ModalEditor::mode() const { return mode_; }

size_t ModalEditor::cursor() const { return cursor_; }

std::u32string ModalEditor::buffer() const { return buffer_; }

std::string ModalEditor::buffer_utf8() const {
  std::string out;
  for (char32_t c : buffer_) {
    append_utf8(&out, c);
  }
  return out;
}

void ModalEditor::set_buffer_utf8(const std::string &text) {
  buffer_.clear();
  size_t pos = 0;
  while (pos < text.size()) {
    size_t before = pos;
    char32_t value = decode_one(text, pos);
    if (pos == before) {
      // Truncated trailing sequence: keep a replacement and move on.
      buffer_.push_back(0xFFFD);
      ++pos;
      continue;
    }
    buffer_.push_back(value);
  }
  // A recalled entry is always ready to edit at the end, in Insert mode,
  // even when the user pressed Up while in Normal mode.
  mode_ = EditMode::Insert;
  cursor_ = buffer_.size();
}

void ModalEditor::recall_history_up() {
  if (history_ == nullptr || history_->size() == 0) {
    return;
  }
  if (!history_active_) {
    history_draft_ = buffer_utf8();
    history_index_ = history_->size();
    history_active_ = true;
  }
  if (history_index_ > 0) {
    --history_index_;
  }
  set_buffer_utf8(history_->at(history_index_));
}

void ModalEditor::recall_history_down() {
  if (history_ == nullptr || !history_active_) {
    return;
  }
  if (history_index_ + 1 < history_->size()) {
    ++history_index_;
    set_buffer_utf8(history_->at(history_index_));
    return;
  }
  // At the newest entry: step past it back to the saved draft.
  history_active_ = false;
  set_buffer_utf8(history_draft_);
  history_draft_.clear();
}

void ModalEditor::clamp_cursor() {
  if (buffer_.empty()) {
    cursor_ = 0;
    return;
  }
  size_t limit =
      (mode_ == EditMode::Insert) ? buffer_.size() : buffer_.size() - 1;
  if (cursor_ > limit) {
    cursor_ = limit;
  }
}

FeedResult ModalEditor::feed(const KeyEvent &event, std::string *out_line) {
  switch (event.kind) {
  case KeyKind::StreamEnd:
    if (buffer_.empty()) {
      return FeedResult::Eof;
    }
    if (out_line != nullptr) {
      *out_line = buffer_utf8();
    }
    return FeedResult::Submitted;
  case KeyKind::Enter:
    if (out_line != nullptr) {
      *out_line = buffer_utf8();
    }
    return FeedResult::Submitted;
  case KeyKind::CtrlD:
    if (buffer_.empty()) {
      return FeedResult::Eof;
    }
    return FeedResult::Continue; // keep editing, like canonical mode.
  case KeyKind::Escape:
    if (!allow_normal_) {
      return FeedResult::Continue;
    }
    mode_ = EditMode::Normal;
    clamp_cursor();
    return FeedResult::Continue;
  case KeyKind::ArrowUp:
    recall_history_up();
    return FeedResult::Continue;
  case KeyKind::ArrowDown:
    recall_history_down();
    return FeedResult::Continue;
  case KeyKind::Backspace:
    if (mode_ == EditMode::Insert) {
      if (cursor_ > 0 && cursor_ <= buffer_.size()) {
        buffer_.erase(buffer_.begin() + (cursor_ - 1));
        --cursor_;
      }
    } else if (cursor_ > 0) {
      --cursor_;
    }
    clamp_cursor();
    return FeedResult::Continue;
  case KeyKind::Char:
    break;
  }

  if (mode_ == EditMode::Insert) {
    if (cursor_ > buffer_.size()) {
      cursor_ = buffer_.size();
    }
    buffer_.insert(buffer_.begin() + cursor_, event.ch);
    ++cursor_;
    return FeedResult::Continue;
  }

  // Normal mode: deliberately small command set. Anything else is a
  // silent no-op.
  switch (event.ch) {
  case U'i':
    mode_ = EditMode::Insert;
    clamp_cursor();
    break;
  case U'h':
    if (cursor_ > 0) {
      --cursor_;
    }
    break;
  case U'l':
    if (!buffer_.empty() && cursor_ + 1 < buffer_.size()) {
      ++cursor_;
    }
    break;
  case U'0':
    cursor_ = 0;
    break;
  case U'$':
    if (!buffer_.empty()) {
      cursor_ = buffer_.size() - 1;
    } else {
      cursor_ = 0;
    }
    break;
  case U'x':
    if (!buffer_.empty() && cursor_ < buffer_.size()) {
      buffer_.erase(buffer_.begin() + cursor_);
    }
    clamp_cursor();
    break;
  default:
    break;
  }
  clamp_cursor();
  return FeedResult::Continue;
}

KeyDecoder::KeyDecoder() = default;

bool KeyDecoder::has_pending_escape() const {
  // An escape sequence is pending when the held bytes start with ESC.
  // Deriving this rather than storing it keeps one source of truth: there
  // is no flag that can disagree with the bytes it describes.
  return !pending_bytes_.empty() &&
         static_cast<unsigned char>(pending_bytes_[0]) == 0x1B;
}

void KeyDecoder::feed(const uint8_t *data, size_t size,
                      std::vector<KeyEvent> *out) {
  if (data == nullptr || out == nullptr) {
    return;
  }
  pending_bytes_.append(reinterpret_cast<const char *>(data), size);
  // A sequence held from a previous chunk resumes through the same loop
  // below, because the held bytes still contain its ESC. There is no
  // special case here to get out of sync with the parser.
  size_t pos = 0;
  while (pos < pending_bytes_.size()) {
    unsigned char c = static_cast<unsigned char>(pending_bytes_[pos]);
    if (c == 0x1B) {
      // Possible CSI start: hold the ESC itself so the next chunk can
      // tell '[' (a sequence) from anything else (a lone Escape).
      if (pos + 1 >= pending_bytes_.size()) {
        pending_bytes_.erase(0, pos);
        return;
      }
      if (pending_bytes_[pos + 1] == '[') {
        size_t end = pos + 2;
        while (end < pending_bytes_.size() &&
               !is_csi_final(static_cast<unsigned char>(pending_bytes_[end]))) {
          ++end;
        }
        if (end >= pending_bytes_.size()) {
          // Incomplete sequence: hold everything from ESC onward.
          pending_bytes_.erase(0, pos);
          return;
        }
        // Only the bare Up and Down forms are mapped. Parameterized CSI
        // (for example Ctrl-Up) stays a no-op, as before.
        const unsigned char final =
            static_cast<unsigned char>(pending_bytes_[end]);
        if (end == pos + 2 && final == 'A') {
          out->push_back(KeyEvent{KeyKind::ArrowUp, 0});
        } else if (end == pos + 2 && final == 'B') {
          out->push_back(KeyEvent{KeyKind::ArrowDown, 0});
        }
        pos = end + 1; // consume the whole sequence.
        continue;
      }
      out->push_back(KeyEvent{KeyKind::Escape, 0});
      ++pos;
      continue;
    }
    if (c == '\r' || c == '\n') {
      out->push_back(KeyEvent{KeyKind::Enter, 0});
      ++pos;
      continue;
    }
    if (c == 0x04) {
      out->push_back(KeyEvent{KeyKind::CtrlD, 0});
      ++pos;
      continue;
    }
    if (c == 0x7F || c == 0x08) {
      out->push_back(KeyEvent{KeyKind::Backspace, 0});
      ++pos;
      continue;
    }
    if (c < 0x20) {
      ++pos; // ignore other control bytes in v1.
      continue;
    }
    size_t before = pos;
    char32_t value = decode_one(pending_bytes_, pos);
    if (pos == before) {
      // Incomplete sequence at chunk end: hold for more input.
      pending_bytes_.erase(0, before);
      return;
    }
    if (value == 0) {
      continue; // decode_one consumed nothing; safety net.
    }
    out->push_back(KeyEvent{KeyKind::Char, value});
  }
  pending_bytes_.clear();
}

void KeyDecoder::flush(std::vector<KeyEvent> *out) {
  if (out == nullptr) {
    return;
  }
  // A held ESC that never grew into a sequence is a lone Escape press.
  if (!pending_bytes_.empty() &&
      static_cast<unsigned char>(pending_bytes_[0]) == 0x1B) {
    pending_bytes_.erase(0, 1);
    out->push_back(KeyEvent{KeyKind::Escape, 0});
  }
  // Any held partial UTF-8 becomes replacement characters.
  for (char c : pending_bytes_) {
    (void)c;
    out->push_back(KeyEvent{KeyKind::Char, 0xFFFD});
  }
  pending_bytes_.clear();
}

bool windows_key_event(char32_t value, KeyEvent *out) {
  if (out == nullptr) {
    return false;
  }
  if (value == 27) {
    *out = KeyEvent{KeyKind::Escape, 0};
  } else if (value == 13) {
    *out = KeyEvent{KeyKind::Enter, 0};
  } else if (value == 8) {
    *out = KeyEvent{KeyKind::Backspace, 0};
  } else if (value == 26) {
    *out = KeyEvent{KeyKind::CtrlD, 0}; // Ctrl-Z: EOF key on Windows.
  } else if (value < 0x20) {
    // Arrows, function keys, Home and End all report UnicodeChar == 0.
    // The POSIX path drops unmapped control bytes; do the same here rather
    // than inserting a NUL into the submitted line.
    return false;
  } else {
    *out = KeyEvent{KeyKind::Char, value};
  }
  return true;
}

InputBackend select_backend(bool opt_in, bool stdin_tty, bool guard_ok) {
  if (!stdin_tty || !guard_ok) {
    return InputBackend::Line;
  }
  return opt_in ? InputBackend::Modal : InputBackend::LineEdit;
}

namespace {

bool stdin_is_tty() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(STDIN_FILENO) != 0;
#endif
}

bool vim_opt_in() {
  return Utils::Config::get_env_var("AGENTWARE_VIM_MODE", "") == "1";
}

// RAII terminal guard: raw-ish input, restored on scope exit. Only the
// flags required for character input change; everything else (notably
// signal generation) is preserved.
class TerminalGuard {
public:
  TerminalGuard() {
#ifdef _WIN32
    valid_ = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &saved_) != 0;
    if (valid_) {
      DWORD raw = saved_;
      raw &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
      valid_ = SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), raw) != 0;
    }
#else
    valid_ = (tcgetattr(STDIN_FILENO, &saved_) == 0);
    if (valid_) {
      struct termios raw = saved_;
      raw.c_lflag &= ~(ECHO | ICANON);
      valid_ = (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0);
    }
#endif
  }

  ~TerminalGuard() {
#ifdef _WIN32
    if (valid_) {
      SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), saved_);
    }
#else
    if (valid_) {
      tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    }
#endif
  }

  TerminalGuard(const TerminalGuard &) = delete;
  TerminalGuard &operator=(const TerminalGuard &) = delete;

  bool ok() const { return valid_; }

private:
  bool valid_ = false;
#ifdef _WIN32
  DWORD saved_ = 0;
#else
  struct termios saved_{};
#endif
};

#ifdef _WIN32
// Windows console gives UTF-16 units; combine surrogate pairs, replacing
// lone surrogates. Returns false on input failure.
bool read_windows_events(ModalEditor *editor, std::string *line,
                         bool *saw_eof) {
  static const int kPollMs = 25;
  DWORD pending = 0;
  if (GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &pending) ==
      0) {
    *saw_eof = true;
    return true;
  }
  if (pending == 0) {
    Sleep(kPollMs);
    return true;
  }
  INPUT_RECORD record{};
  DWORD got = 0;
  if (ReadConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &got) ==
          0 ||
      got == 0) {
    *saw_eof = true;
    return true;
  }
  if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
    return true;
  }
  // Arrow keys report UnicodeChar == 0 on Windows, so history navigation
  // is driven by the virtual key code instead of the character.
  const WORD vk = record.Event.KeyEvent.wVirtualKeyCode;
  if (vk == VK_UP || vk == VK_DOWN) {
    const KeyEvent event{vk == VK_UP ? KeyKind::ArrowUp : KeyKind::ArrowDown,
                         0};
    FeedResult result = editor->feed(event, line);
    if (result == FeedResult::Submitted || result == FeedResult::Eof) {
      *saw_eof = (result == FeedResult::Eof);
      return false;
    }
    return true;
  }
  wchar_t wc = record.Event.KeyEvent.uChar.UnicodeChar;
  char32_t value = 0;
  if (wc >= 0xD800 && wc <= 0xDBFF) {
    // High surrogate: blocking read for the low half (console input is
    // local and prompt-driven; a missing half ends input).
    if (ReadConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &got) ==
            0 ||
        got == 0 || record.EventType != KEY_EVENT ||
        !record.Event.KeyEvent.bKeyDown) {
      value = 0xFFFD;
    } else {
      wchar_t low = record.Event.KeyEvent.uChar.UnicodeChar;
      if (low >= 0xDC00 && low <= 0xDFFF) {
        value = 0x10000 + ((static_cast<char32_t>(wc) - 0xD800) << 10) +
                (static_cast<char32_t>(low) - 0xDC00);
      } else {
        value = 0xFFFD;
      }
    }
  } else if (wc >= 0xDC00 && wc <= 0xDFFF) {
    value = 0xFFFD;
  } else {
    value = static_cast<char32_t>(wc);
  }
  KeyEvent event{KeyKind::Char, 0};
  if (!windows_key_event(value, &event)) {
    // Special key with no text and no mapping: nothing to insert.
    return true;
  }
  FeedResult result = editor->feed(event, line);
  if (result == FeedResult::Submitted || result == FeedResult::Eof) {
    *saw_eof = (result == FeedResult::Eof);
    return false; // done reading.
  }
  return true;
}
#else
// Feeds queued events until one submits or ends the stream. Events after
// that one stay queued for the next prompt, so a paste that crosses a line
// boundary loses nothing.
bool process_queued_events(ModalEditor *editor, std::deque<KeyEvent> *pending,
                           std::string *line, bool *saw_eof) {
  while (!pending->empty()) {
    const KeyEvent event = pending->front();
    pending->pop_front();
    const FeedResult result = editor->feed(event, line);
    if (result == FeedResult::Submitted) {
      std::cout << "\n";
      return false;
    }
    if (result == FeedResult::Eof) {
      *saw_eof = true;
      return false;
    }
  }
  return true;
}

// Returns false when input is exhausted (caller returns the line or EOF).
bool read_posix_chunk(ModalEditor *editor, KeyDecoder *decoder,
                      std::deque<KeyEvent> *pending, std::string *line,
                      bool *saw_eof) {
  if (!pending->empty()) {
    return process_queued_events(editor, pending, line, saw_eof);
  }

  std::vector<KeyEvent> events;
  // Startup prompts read through std::cin, whose buffer can hold bytes the
  // raw descriptor no longer has. Consume those first; they precede
  // anything still waiting in the kernel. If the stream cannot supply them
  // (for example after a failed read), fall through to the descriptor.
  bool drained = false;
  const std::streamsize buffered = std::cin.rdbuf()->in_avail();
  if (buffered > 0) {
    std::string bytes(static_cast<size_t>(buffered), '\0');
    std::cin.read(&bytes[0], buffered);
    const std::streamsize got = std::cin.gcount();
    if (got > 0) {
      decoder->feed(reinterpret_cast<const uint8_t *>(bytes.data()),
                    static_cast<size_t>(got), &events);
      drained = true;
    }
  }
  if (!drained) {
    uint8_t chunk[256];
    ssize_t count = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (count == 0) {
      KeyEvent end{KeyKind::StreamEnd, 0};
      FeedResult result = editor->feed(end, line);
      *saw_eof = (result == FeedResult::Eof);
      return false;
    }
    if (count < 0) {
      if (errno == EINTR) {
        return true;
      }
      *saw_eof = true;
      return false; // terminal error ends the loop, like getline failure.
    }
    decoder->feed(chunk, static_cast<size_t>(count), &events);
  }

  if (decoder->has_pending_escape()) {
    // Grace period so an ESC split across reads is not mistaken for a
    // lone Escape before arrow-key bytes arrive.
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval wait = {0, 50000};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &wait) > 0) {
      pending->insert(pending->end(), events.begin(), events.end());
      return true; // more bytes coming; loop back to read them.
    }
    decoder->flush(&events);
  }
  pending->insert(pending->end(), events.begin(), events.end());
  return process_queued_events(editor, pending, line, saw_eof);
}
#endif

void redraw_line(const std::string &prompt, const ModalEditor &editor) {
  // Minimal redraw: carriage return, prompt, buffer, clear to end of line,
  // then move back by code-point count. Wide glyphs may misalign in v1;
  // cursor movement is documented as code-point based.
  const std::string text = editor.buffer_utf8();
  size_t total_points = 0;
  for (unsigned char c : text) {
    if ((c & 0xC0) != 0x80) {
      ++total_points;
    }
  }
  const size_t back =
      total_points > editor.cursor() ? total_points - editor.cursor() : 0;
  std::cout << "\r" << prompt << text << "\x1b[K";
  if (back > 0) {
    std::cout << "\x1b[" << back << "D";
  }
  std::cout << std::flush;
}

} // namespace

ReadResult read_prompt_line(const std::string &prompt) {
  // One ring per process: the REPL calls this once per prompt, and history
  // must survive between calls. Nothing is persisted to disk.
  static History session_history;

  std::cout << prompt << std::flush;
  const InputBackend backend =
      select_backend(vim_opt_in(), stdin_is_tty(), true);
  if (backend == InputBackend::Line) {
    std::string line;
    if (!std::getline(std::cin, line)) {
      return ReadResult{ReadStatus::Eof, ""};
    }
    session_history.add(line);
    return ReadResult{ReadStatus::Line, line};
  }

  TerminalGuard guard;
  if (!guard.ok()) {
    std::string line;
    if (!std::getline(std::cin, line)) {
      return ReadResult{ReadStatus::Eof, ""};
    }
    session_history.add(line);
    return ReadResult{ReadStatus::Line, line};
  }

  ModalEditor editor(&session_history, backend == InputBackend::Modal);
  std::string line;
  bool saw_eof = false;
#ifdef _WIN32
  while (read_windows_events(&editor, &line, &saw_eof)) {
    redraw_line(prompt, editor);
  }
#else
  // Decoder and event queue survive between prompts: a paste can contain
  // several lines, and the bytes after the first Enter belong to the next
  // read.
  static KeyDecoder decoder;
  static std::deque<KeyEvent> pending_events;
  while (
      read_posix_chunk(&editor, &decoder, &pending_events, &line, &saw_eof)) {
    redraw_line(prompt, editor);
  }
#endif
  if (saw_eof && line.empty()) {
    return ReadResult{ReadStatus::Eof, ""};
  }
  session_history.add(line);
  return ReadResult{ReadStatus::Line, line};
}

} // namespace Input
} // namespace Utils
