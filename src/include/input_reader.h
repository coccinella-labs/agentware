#pragma once

// Optional Vim-style editing for the main REPL prompt (issue #160).
//
// Supported keys (v1, documented here because this is the contract):
//   Insert mode: printable input, Backspace, Enter submits.
//   Normal mode: h l 0 $ move, x deletes under cursor, i returns to
//     insert, Enter submits. Other keys are silent no-ops.
//   Ctrl-D: Eof on an empty buffer, no-op with pending input.
// Limitations v1: no arrow keys (escape sequences ignored), no visual
// mode/macros/registers/undo, no completion/history, cursor movement is
// by Unicode scalar value (not grapheme clusters), invalid bytes become
// U+FFFD. Opt-in only; pipes and redirected stdin always use getline.

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
  std::string line;  // valid only when status == Line (may be empty).
};

enum class EditMode { Insert, Normal };

// Feed result from the editing state machine.
enum class FeedResult { Continue, Submitted, Eof };

// Input events. CtrlD is the 0x04 byte (or synthesized equivalent);
// StreamEnd is actual stream exhaustion (read()==0 / error), distinct
// from CtrlD per the agreed EOF contract.
enum class KeyKind { Char, Escape, Enter, Backspace, CtrlD, StreamEnd };

struct KeyEvent {
  KeyKind kind;
  char32_t ch = 0;  // valid only for Char.
};

// Pure editing state machine: no TTY, no syscalls, fully unit-testable.
// Buffer holds Unicode scalar values. Cursor is a code-point index with
// insert range [0, len], normal range [0, len-1], sentinel 0 when empty.
class ModalEditor {
 public:
  ModalEditor();

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

  void clamp_cursor();
};

// Decodes a byte stream into key events. Holds a trailing lone ESC or a
// partial UTF-8 sequence until more bytes arrive or flush() is called.
// CSI escape sequences (arrows etc.) are consumed and ignored in v1.
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

// Backend selection. Modal input requires all three; everything else
// uses plain getline. Pure function of its inputs for table testing.
enum class InputBackend { Line, Modal };

InputBackend select_backend(bool opt_in, bool stdin_tty, bool guard_ok);

// Reads one prompt line, printing `prompt` first (same bytes as today).
// Falls back to plain getline unless select_backend forces Modal and the
// terminal guard constructs. Never throws for input errors.
ReadResult read_prompt_line(const std::string &prompt);

}  // namespace Input
}  // namespace Utils
