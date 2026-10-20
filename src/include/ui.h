#pragma once
#include "config.h" // For AGENTWARE_API
#include <atomic>
#include <string>
#include <vector>

namespace Utils {
// Color namespace. The constants are mutable std::strings on purpose:
// terminal_setup flips them on Windows consoles that refuse VT
// processing (colors-off fallback); reading sites stay untouched.
namespace Color {
extern AGENTWARE_API std::string RESET;
extern AGENTWARE_API std::string GREEN;
extern AGENTWARE_API std::string YELLOW;
extern AGENTWARE_API std::string RED;
extern AGENTWARE_API std::string CYAN;
extern AGENTWARE_API std::string BOLD;
extern AGENTWARE_API std::string DIM;
extern AGENTWARE_API std::string PINK;
// Subdued orange for active work (a running tool call, a waiting model).
extern AGENTWARE_API std::string ACCENT;
} // namespace Color

// Glyph namespace: the UI's own box-drawing and symbol chrome, mutable
// for the same console-fallback reason as Color. Tool data output (the
// codebase tree, fetched pages) is not routed through these.
namespace Glyph {
extern AGENTWARE_API std::string HLINE;     // box horizontal ─
extern AGENTWARE_API std::string VLINE;     // box vertical │
extern AGENTWARE_API std::string CORNER_TL; // ╭
extern AGENTWARE_API std::string CORNER_TR; // ╮
extern AGENTWARE_API std::string CORNER_BL; // ╰
extern AGENTWARE_API std::string CORNER_BR; // ╯
extern AGENTWARE_API std::string ARROW;     // prompt marker ❯
extern AGENTWARE_API std::string SPARK;     // turn-complete marker ✳
extern AGENTWARE_API std::string BULLET;    // tool/response bullet ●
extern AGENTWARE_API std::string MIDDOT;    // brand separator ·
} // namespace Glyph

// UI namespace
namespace UI {
// Core functions
AGENTWARE_API void print_logo();
AGENTWARE_API void print_help();
AGENTWARE_API void print_enterprise_status();
AGENTWARE_API void spinner(const std::string &message, int duration_ms);
AGENTWARE_API void spinner(std::atomic<bool> &done); // For threaded spinner
// Threaded spinner with a label: the label stays on the spinner line while
// the frame cycles, so a waiting model reads as active work. No-op on
// non-interactive output, like the unlabeled threaded spinner.
AGENTWARE_API void spinner(std::atomic<bool> &done, const std::string &label);
// Terminal-console fallback: colors off and glyphs swapped for ASCII
// (>, +-|, *, o, .). Called when the console cannot do VT processing
// (legacy Windows conhost). After this, colors_enabled() also reports
// false, so every existing colors_enabled() branch takes its ASCII
// path.
AGENTWARE_API void enable_ascii_fallback();

// Status messages
AGENTWARE_API void print_success(const std::string &message);
AGENTWARE_API void print_error(const std::string &message);
AGENTWARE_API void print_warning(const std::string &message);
AGENTWARE_API void print_info(const std::string &message);

// Utility functions
AGENTWARE_API void print_divider();
AGENTWARE_API void print_quick_help();
// Session renderers for the activity surface. Tool calls print as
// "● Tool(args)" and results as a "└ "-prefixed block, the same
// presentation the reference design uses; responses print as a
// "● "-marked assistant block. Nothing here invents content: callers
// pass the exact tool name, argument text, and body to render.
AGENTWARE_API void print_tool_call(const std::string &tool,
                                   const std::string &args);
AGENTWARE_API void print_tool_result(const std::string &result);
AGENTWARE_API void print_response(const std::string &response);
// Full-width outline around the bottom input line. Only emitted on an
// interactive terminal; pipes get the prompt alone.
AGENTWARE_API void print_input_frame_top();
AGENTWARE_API void print_input_frame_bottom();
// Welcome card for the empty state: version, mode, model, and the
// working directory, plus a couple of honest hints. Only on an
// interactive terminal; the pipe path keeps the compact legacy banner.
AGENTWARE_API void print_welcome_panel(const std::string &mode,
                                       const std::string &model,
                                       const std::string &cwd);
// Echo of what the user just submitted, styled as the transcript's
// request line with a full-width highlight bar (interactive terminals
// only; pipes do not re-print what a script already printed).
AGENTWARE_API void print_user_line(const std::string &user_input);
// Completion status measured from the real call duration. No token
// counts or interrupt hints the runtime does not expose.
AGENTWARE_API void print_turn_status(int seconds);
// Change rendering fed strictly from operation data: removed lines then
// added lines, like a diff. Callers pass only text the operation saw.
AGENTWARE_API void print_diff_removed(const std::string &text);
AGENTWARE_API void print_diff_added(
    const std::string &text); // True when stdout renders to an interactive
                              // terminal. Centralizes the
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
AGENTWARE_API std::vector<std::string>
status_lines(int width, const std::string &mode, const std::string &model);
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
