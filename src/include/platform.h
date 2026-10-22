#pragma once

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#define popen _popen
#define pclose _pclose
#define NULL_DEVICE "NUL"
#define PATH_SEPARATOR "\\"
#define SHELL_REDIRECT " 2>NUL"
#define SHELL_REDIRECT_BOTH " >NUL 2>&1"
#else
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <termios.h>
#define NULL_DEVICE "/dev/null"
#define PATH_SEPARATOR "/"
#define SHELL_REDIRECT " 2>/dev/null"
#define SHELL_REDIRECT_BOTH " >/dev/null 2>&1"
#endif

#include <cstdio>
#include <cstdlib>
#include <string>

namespace Utils {
namespace Platform {

// Cross-platform command execution
inline FILE *open_process(const std::string &command, const char *mode) {
  // Callers either shell-quote interpolated values, validate paths via
  // is_safe_shell_path, or (for command_service) intentionally execute the
  // user's shell command as the cmd: tool. A full execvp refactor would remove
  // the shell entirely; tracked as a follow-up.
  //
  // The CodeQL suppression goes on the sink's OWN line. The C/C++ extractor
  // anchors this alert on the argument expression, and a suppression comment
  // on the preceding line was not honored: code scanning alert 45 stayed
  // open with the tag one line above the sink.
  return popen(command.c_str(), mode); // codeql[cpp/command-line-injection]
}

// Shell-safety check for paths interpolated into shell commands.
// Rejects content that could break out of double quotes or chain commands.
inline bool is_safe_shell_path(const std::string &path) {
  static const std::string forbidden = "\"$`\\;&|<>\n\r";
  return path.find_first_of(forbidden) == std::string::npos;
}

inline int close_process(FILE *pipe) { return pclose(pipe); }

// The exit code of a finished child.
//
// pclose returns the wait status, which encodes the exit code in its
// low byte: a process that exited with 1 gives 256, not 1. Comparing
// such a value against an exit code is a trap, so decode it here. On
// Windows pclose already returns the exit code.
inline int process_exit_code(int status) {
#ifdef _WIN32
  return status;
#else
  if (status == -1)
    return -1;
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return -1; // killed by a signal, or not a normal exit
#endif
}

// Cross-platform null device
inline std::string get_null_device() { return NULL_DEVICE; }

// Cross-platform path separator
inline std::string get_path_separator() { return PATH_SEPARATOR; }

// Cross-platform shell redirection
inline std::string get_shell_redirect() { return SHELL_REDIRECT; }

inline std::string get_shell_redirect_both() { return SHELL_REDIRECT_BOTH; }

// Check if running on Windows
inline bool is_windows() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

// Width of the terminal attached to stdout, in columns.
//
// Returns 0 when the width cannot be determined: stdout is not a terminal, or
// the query failed. Callers apply their own fallback in that case. COLUMNS is
// deliberately not consulted; it is frequently stale in non-interactive shells
// and is not the project's adopted source of terminal geometry.
inline int terminal_width() {
#ifdef _WIN32
  // Not implemented for Windows yet. Layout code calls through this seam so
  // support can be added without touching the layout itself.
  return 0;
#else
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
    return 0;
  }
  // A zero column count means the terminal did not report a usable width.
  if (ws.ws_col == 0) {
    return 0;
  }
  return static_cast<int>(ws.ws_col);
#endif
}

// Environment mutation that works on both platforms. MSVC provides no
// setenv or unsetenv, and _putenv_s with an empty value deletes the variable,
// which is the behaviour unsetenv has on POSIX.
inline void set_env(const char *name, const char *value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

inline void unset_env(const char *name) {
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

// True when stdin is an interactive terminal, which is when it echoes the
// user's Enter as a line break. When it is not, output code must supply the
// newline itself or consecutive prompts run together on one line.
inline bool stdin_is_tty() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(STDIN_FILENO) != 0;
#endif
}

} // namespace Platform
} // namespace Utils
