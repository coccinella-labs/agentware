#pragma once
#include "config.h" // For AGENTWARE_API
#include <atomic>
#include <string>

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
} // namespace UI
} // namespace Utils
