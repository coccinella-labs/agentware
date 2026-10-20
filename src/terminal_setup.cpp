// Windows console capability bring-up for the terminal UI.
//
// The UI emits UTF-8 bytes and ANSI/VT sequences unconditionally (see
// src/ui.cpp). Legacy Windows consoles need two accommodations that no
// other platform needs: the output code page set to UTF-8, and
// ENABLE_VIRTUAL_TERMINAL_PROCESSING on the output handles (conhost
// otherwise prints the escapes literally). Windows Terminal handles
// both regardless, so the setup is also correct there.
//
// If VT cannot be enabled, setup() reports failure and the caller
// flips the UI to the ASCII/colors-off fallback instead of printing
// mojibake boxes.
//
// stderr is treated exactly like stdout: same code page (console-wide
// on Windows), VT enabled on its own handle, saved and restored
// separately — error paths print colored output via std::cerr too.

#include "terminal_setup.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdlib>
#include <csignal>

namespace Utils::Terminal {

#ifdef _WIN32
namespace {
UINT g_saved_cp = 0;
DWORD g_saved_out_mode = 0;
DWORD g_saved_err_mode = 0;
bool g_saved_out = false;
bool g_saved_err = false;
bool g_restored = false;

} // namespace

void restore() {
  if (g_restored) {
    return;
  }
  g_restored = true;
  if (g_saved_out) {
    if (HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE)) {
      SetConsoleMode(out, g_saved_out_mode);
    }
    g_saved_out = false;
  }
  if (g_saved_err) {
    if (HANDLE err = GetStdHandle(STD_ERROR_HANDLE)) {
      SetConsoleMode(err, g_saved_err_mode);
    }
    g_saved_err = false;
  }
  if (g_saved_cp != 0) {
    SetConsoleOutputCP(g_saved_cp);
    g_saved_cp = 0;
  }
}

// Runs on the process's Ctrl-C delivery: restores console state, then
// re-raises the default action so termination semantics stay identical
// to the no-handler behavior. Notes: Windows ctrl handling does not
// impose POSIX async-signal-safety rules, and restore() only touches
// the process's own console state before the process terminates.
void ctrl_c_handler(int) {
  restore();
  std::signal(SIGINT, SIG_DFL);
  std::raise(SIGINT);
}
#else

void restore() {
  // POSIX terminals carry no process-owned console state worth
  // restoring; input-mode toggles live in Input::TerminalGuard.
}

void ctrl_c_handler(int) {}
#endif

bool setup() {
#ifdef _WIN32
  // Pipes and files: nothing to enable or save; the non-tty output
  // paths in the UI already gate themselves.
  DWORD out_mode = 0;
  DWORD err_mode = 0;
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (!GetConsoleMode(out, &out_mode)) {
    return true;
  }
  g_saved_cp = GetConsoleOutputCP();

  const bool cp_ok = SetConsoleOutputCP(CP_UTF8) != 0;
  if (cp_ok) {
    // CP_UTF8 affects the whole console; stderr shares it by
    // definition, which is the consistency the spec asks for.
  }

  g_saved_out_mode = out_mode;
  g_saved_out = true;
  const bool vt_out =
      SetConsoleMode(out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
  g_saved_err_mode = 0;
  g_saved_err = false;
  bool vt_err = true;
  if (GetConsoleMode(err, &err_mode)) {
    g_saved_err_mode = err_mode;
    g_saved_err = true;
    vt_err =
        SetConsoleMode(err, err_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
  }
  std::atexit(restore);

  if (!(vt_out && vt_err)) {
    // Legacy console that refuses VT: roll state back and tell the
    // caller to use the ASCII, colors-off fallback.
    restore();
    return false;
  }
#endif
  return true;
}

void install_ctrl_c_restore() {
#ifdef _WIN32
  std::signal(SIGINT, ctrl_c_handler);
#endif
}

} // namespace Utils::Terminal
