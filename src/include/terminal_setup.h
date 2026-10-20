#pragma once
#include "config.h" // For AGENTWARE_API

namespace Utils::Terminal {
// Bring the console up for the terminal UI (Windows only; a no-op that
// returns true elsewhere). On Windows: output code page -> UTF-8 and
// VT processing -> enabled on both stdout and stderr. Returns false
// when the console cannot do VT (legacy conhost refusing the flag); the
// caller should then switch the UI to its ASCII, colors-off fallback
// (UI::enable_ascii_fallback). When stdout is not a console (pipes,
// files), nothing is touched and true is returned.
//
// The original code page and console modes are saved in-process and
// restored by restore(), which is registered for normal exit and, via
// install_ctrl_c_restore(), for Ctrl-C.
AGENTWARE_API bool setup();

// Register the Ctrl-C handler that restores the console state before
// re-raising the default termination. Windows-only; call once after a
// successful setup().
AGENTWARE_API void install_ctrl_c_restore();

// Restore the saved console code page and modes. Idempotent; called at
// normal exit (atexit) and from the Ctrl-C handler.
AGENTWARE_API void restore();
} // namespace Utils::Terminal
