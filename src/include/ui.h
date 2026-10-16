#pragma once
#include "config.h" // For AGENTWARE_API
#include <atomic>
#include <string>
#include <vector>

namespace Utils {
// Color namespace
namespace Color {
extern AGENTWARE_API const std::string RESET;
extern AGENTWARE_API const std::string GREEN;
extern AGENTWARE_API const std::string YELLOW;
extern AGENTWARE_API const std::string RED;
extern AGENTWARE_API const std::string CYAN;
extern AGENTWARE_API const std::string BOLD;
extern AGENTWARE_API const std::string DIM;
extern AGENTWARE_API const std::string PINK;
} // namespace Color

// UI namespace
namespace UI {
// Core functions
AGENTWARE_API void print_logo();
AGENTWARE_API void print_help();
AGENTWARE_API void print_enterprise_status();
AGENTWARE_API void spinner(const std::string &message, int duration_ms);
AGENTWARE_API void spinner(std::atomic<bool> &done); // For threaded spinner

// Status messages
AGENTWARE_API void print_success(const std::string &message);
AGENTWARE_API void print_error(const std::string &message);
AGENTWARE_API void print_warning(const std::string &message);
AGENTWARE_API void print_info(const std::string &message);

// Utility functions
AGENTWARE_API void print_divider();
AGENTWARE_API void print_quick_help();
// True when stdout renders to an interactive terminal. Centralizes the
// TTY check so output code can avoid control sequences in pipes.
AGENTWARE_API bool interactive_output();
// Single source of truth for whether ANSI escapes may be emitted: stdout must
// be a terminal and NO_COLOR must be unset. TERM is deliberately not consulted;
// a real TTY colors even when TERM is "dumb". Any subsystem that emits color,
// including ThemeService, must defer to this rather than re-deciding, so the
// two policies cannot drift apart again.
AGENTWARE_API bool colors_enabled();
AGENTWARE_API void print_system_info(const std::string &mode,
                                     const std::string &model);
AGENTWARE_API void print_ready_interface(const std::string &mode,
                                         const std::string &model);
AGENTWARE_API std::string prompt_user(const std::string &prompt_text);

// ===== Width-aware layout =====
//
// The layout width is the detected terminal width, or kFallbackLayoutWidth when
// detection fails. The fallback is a layout assumption for streams whose width
// is unknown, such as pipes and CI logs. It is not a claim that the
// destination is that wide; detected width always wins.

// Applied when the terminal width cannot be determined. Deliberately
// conventional: a narrower fallback would wrap excessively in logs that are
// often read in wider terminals.
static constexpr int kFallbackLayoutWidth = 80;

// Narrowest width the layout will attempt to honor before falling back to a
// simple one-entry-per-line form.
static constexpr int kMinLayoutWidth = 24;

// Detected terminal width, or kFallbackLayoutWidth when detection fails.
AGENTWARE_API int layout_width();

// Pure layout builders. Each takes an explicit width so the output can be
// asserted directly by tests at any width, including widths no test terminal
// has. The print_* functions below are thin emitters over these.
AGENTWARE_API std::vector<std::string> help_lines(int width);
AGENTWARE_API std::vector<std::string> status_lines(int width,
                                                   const std::string &mode,
                                                   const std::string &model);
AGENTWARE_API std::vector<std::string> quick_help_lines(int width);

// A prompt wrapped to the layout width, with continuation lines indented.
// Returned lines exclude the trailing space that separates the prompt from the
// cursor, so the caller can re-append it.
AGENTWARE_API std::vector<std::string> prompt_lines(int width,
                                                   const std::string &prompt);

// Emits prompt_lines for the detected width and leaves the cursor after the
// prompt, without emitting a trailing newline.
AGENTWARE_API void print_wrapped_prompt(const std::string &prompt);
} // namespace UI
} // namespace Utils
