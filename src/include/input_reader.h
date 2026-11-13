#pragma once

// Line editing for the main REPL prompt: interactive history plus optional
// Vim-style normal mode (issues #160 and #265).
//
// Supported keys, documented here because this is the contract:
//   Insert mode: printable input, Backspace, Enter submits.
//   Up/Down: walk the session history in either mode. Down at the newest
//     entry restores the draft typed before history navigation began.
//   Normal mode: h l 0 $ move, x deletes under cursor, i returns to
//     insert, Enter submits. Other keys are silent no-ops.
//   Ctrl-D: Eof on an empty buffer, no-op with pending input.
// Limitations: no visual mode/macros/registers/undo, no completion, no
// persistent history file, cursor movement is by Unicode scalar value
// (not grapheme clusters), invalid bytes become U+FFFD. Interactive
// terminals use a line editor; pipes and redirected stdin use getline.
// Normal mode is opt-in via AGENTWARE_VIM_MODE=1.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Utils {
namespace Input {

// Outcome of one prompt read. Eof and Error both end the REPL loop,
// matching the existing `if (!std::getline(...)) break;` behavior.
enum class ReadStatus { Line, Eof, Error };

struct ReadResult {
  ReadStatus status;
  std::string line; // valid only when status == Line (may be empty).
};

// Session-scoped, in-memory history shared by the interactive backends.
// Empty lines and lines containing a raw ESC byte are never stored, so a
// stray escape sequence from a paste cannot enter the ring. Bounded so a
// long session cannot grow without limit.
class History {
public:
  static constexpr size_t k_max_entries = 100;

  void add(const std::string &line);
  size_t size() const;
  const std::string &at(size_t index) const; // index must be < size().
  void clear();

private:
  std::vector<std::string> entries_;
};

enum class EditMode { Insert, Normal };

// Feed result from the editing state machine.
enum class FeedResult { Continue, Submitted, Eof };

// Input events. CtrlD is the 0x04 byte (or synthesized equivalent);
// StreamEnd is actual stream exhaustion (read()==0 / error), distinct
// from CtrlD per the agreed EOF contract.
enum class KeyKind {
  Char,
  Escape,
  Enter,
  Backspace,
  CtrlD,
  StreamEnd,
  ArrowUp,
  ArrowDown
};

struct KeyEvent {
  KeyKind kind;
  char32_t ch = 0; // valid only for Char.
};

// Pure editing state machine: no TTY, no syscalls, fully unit-testable.
// Buffer holds Unicode scalar values. Cursor is a code-point index with
// insert range [0, len], normal range [0, len-1], sentinel 0 when empty.
class ModalEditor {
public:
  ModalEditor();
  // `history` may be null (unit tests and non-interactive fallbacks).
  // `allow_normal` turns Escape into a no-op when false, which is the
  // plain interactive line editor selected without AGENTWARE_VIM_MODE.
  ModalEditor(History *history, bool allow_normal);

  EditMode mode() const;
  size_t cursor() const;
  std::u32string buffer() const;
  std::string buffer_utf8() const;

  // Feed one event. Submitted sets out_line (possibly empty); Eof ends
  // the loop. StreamEnd with pending input submits once; the next read
  // observes the dead stream and reports Eof, mirroring getline.
  FeedResult feed(const KeyEvent &event, std::string *out_line);

private:
  EditMode mode_ = EditMode::Insert;
  std::u32string buffer_;
  size_t cursor_ = 0;

  History *history_ = nullptr;
  bool allow_normal_ = true;
  bool history_active_ = false;
  size_t history_index_ = 0;
  std::string history_draft_;

  void clamp_cursor();
  void set_buffer_utf8(const std::string &text);
  void recall_history_up();
  void recall_history_down();
};

// Decodes a byte stream into key events. Holds a trailing lone ESC or a
// partial UTF-8 sequence until more bytes arrive or flush() is called.
// CSI Up/Down become ArrowUp/ArrowDown; other CSI sequences are consumed
// and ignored.
//
// An incomplete escape sequence is held as its actual bytes, never as a
// flag. A flag cannot record how much of the sequence arrived, so a split
// anywhere other than the first byte had to be resumed from state the
// parser no longer had. Keeping the bytes means every split point resumes
// through the same CSI path.
class KeyDecoder {
public:
  KeyDecoder();

  void feed(const uint8_t *data, size_t size, std::vector<KeyEvent> *out);
  void flush(std::vector<KeyEvent> *out);
  bool has_pending_escape() const;

private:
  std::string pending_bytes_;
};

// Maps a decoded Windows console character to a key event. Returns false
// when the value carries no text and no mapped key, which is how special
// keys arrive: arrows, function keys, Home and End all report
// UnicodeChar == 0 on Windows. Declared outside the Win32 guard so the
// mapping is testable on any platform.
bool windows_key_event(char32_t value, KeyEvent *out);

// Backend selection. Interactive terminals use LineEdit, which has history
// and arrow keys but no Vim modes; opting in selects Modal instead. Pipes,
// redirected stdin, and a failed terminal guard use plain getline. Pure
// function of its inputs for table testing.
enum class InputBackend { Line, LineEdit, Modal };

InputBackend select_backend(bool opt_in, bool stdin_tty, bool guard_ok);

// Reads one prompt line, printing `prompt` first (same bytes as today).
// Interactive input uses the line editor with session history; opting in
// to Vim mode adds Normal mode. Falls back to plain getline when stdin is
// not a terminal or the terminal guard fails. Never throws for input
// errors.
ReadResult read_prompt_line(const std::string &prompt);

} // namespace Input
} // namespace Utils
