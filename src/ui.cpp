#include "ui.h"
// After ui.h: that header includes config.h, which defines NOMINMAX on
// Windows. platform.h includes <windows.h>, and without NOMINMAX already
// defined, windef.h defines min/max as macros and std::max stops parsing.
#include "platform.h"
#include "version.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Utils {

namespace {
// Terminal detection helper. Kept file-local; callers use the public
// interactive_output() and colors_enabled() predicates below.
bool stdout_is_tty() {
#ifdef _WIN32
  return _isatty(_fileno(stdout)) != 0;
#else
  return isatty(STDOUT_FILENO) != 0;
#endif
}
std::string code(const char *seq) { return UI::colors_enabled() ? seq : ""; }
} // namespace

bool UI::interactive_output() { return stdout_is_tty(); }

// NO_COLOR disables color without disabling any other output. This is the
// single source of truth for the whole process; ThemeService defers to it so
// the two color policies cannot drift apart.
bool UI::colors_enabled() {
  if (std::getenv("NO_COLOR") != nullptr) {
    return false;
  }
  return stdout_is_tty();
}

// ===== Color Codes =====
namespace Color {
// Exported color constants
AGENTWARE_API const std::string RESET = code("\033[0m");
AGENTWARE_API const std::string GREEN = code("\033[32m");
AGENTWARE_API const std::string YELLOW = code("\033[33m");
AGENTWARE_API const std::string RED = code("\033[31m");
AGENTWARE_API const std::string CYAN = code("\033[36m");
AGENTWARE_API const std::string BOLD = code("\033[1m");
AGENTWARE_API const std::string DIM = code("\033[2m");
// Logo color (matches Coccinella Labs pink #F34B7D)
AGENTWARE_API const std::string PINK = code("\033[38;2;243;75;125m");
// Subdued orange for active work.
AGENTWARE_API const std::string ACCENT = code("\033[38;2;214;141;84m");
} // namespace Color

// ===== Logo =====
// A restrained header: one brand line and a divider. The oversized block
// banner is gone; the width-aware layout keeps small terminals usable.
void UI::print_logo() {
  std::cout << Color::PINK << Color::BOLD << "Agentware" << Color::RESET
            << Color::DIM << "  ·  Coccinella Labs" << Color::RESET << "\n";
  print_divider();
}

// ===== Help Menu =====
namespace {

// One help row: command syntax and its explanation. Command syntax is
// reproduced verbatim and never truncated, because a user who cannot read the
// full syntax cannot type it.
struct HelpEntry {
  const char *command;
  const char *description;
};

struct HelpSection {
  const char *title;
  std::vector<HelpEntry> entries;
};

const std::vector<HelpSection> &help_sections() {
  static const std::vector<HelpSection> sections = {
      {"Files",
       {{"@<path> <prompt>", "Inject a file or directory into chat"},
        {"read:<file>[:start[:count]]", "Read a file or range"},
        {"write:<file> <content>", "Write a file"},
        {"replace:<file>:<old>:<new>[:count]", "Replace text"},
        {"grep:<pattern>[:dir[:filter]]", "Search text"},
        {"/files <path...> [options]", "Read multiple files"}}},
      {"Sessions",
       {{"/chat save|resume <tag>", "Save or resume a conversation"},
        {"/chat list", "List saved conversations"},
        {"/compress", "Compress context"},
        {"remember:<fact>", "Remember fact"},
        {"memory | forget | clear", "Show or clear memory"},
        {"/memory show|add <text>", "Inspect or add memory"},
        {"/context show|refresh|create", "Manage project context"}}},
      {"Project",
       {{"analyze: | components: | todos: | tree:", "Inspect the project"},
        {"git:status | git:log | git:analyze", "Inspect Git"}}},
      {"Web and GitHub",
       {{"/fetch <url> [text|json|raw]", "Fetch content"},
        {"search:<query>", "Search the web"},
        {"github:repo:<owner>/<repo>", "Show repository details"},
        {"github:issues:<owner>/<repo>", "List issues"},
        {"github:health:<owner>/<repo>", "Run repository health checks"},
        {"github:threads:<owner>/<repo>:<pr>[:query]", "List review threads"},
        {"/mcp <command>", "Inspect MCP configuration"}}},
      {"Checkpoints",
       {{"/checkpoint <command>",
         "Create, list, inspect, or delete checkpoints"},
        {"/restore [id]", "List or restore a checkpoint"}}},
      {"Settings and diagnostics",
       {{"/theme <command>", "Manage themes"},
        {"/auth <command>", "Manage provider credentials"},
        {"/sandbox <command>", "Manage or run sandboxes"},
        {"/error <command>", "Inspect diagnostics"},
        {"/stats", "Show session statistics"}}},
      {"System",
       {{"cmd:<command>", "Run a command"},
        {"!<command>", "Run a shell command"},
        {"/tools", "Show direct tools"},
        {"help | /help | /?", "Show command help"},
        {"version | exit | quit", "Show version or leave"}}},
  };
  return sections;
}

// Quick-help starters. Each appears exactly once; the previous listing repeated
// search:query, cmd:command and read:file across its two lines. Both spellings
// for leaving are listed so neither has to be discovered through full help.
const std::vector<std::string> &quick_help_tokens() {
  static const std::vector<std::string> tokens = {
      "help",      "version",         "search:query", "cmd:command",
      "read:file", "write:file text", "quit",         "exit"};
  return tokens;
}

const char *const kIndent = "  ";
const char *const kContIndent = "    ";
const char *const kSeparator = " - ";
// Description room preferred before the description column is pulled left.
constexpr size_t kPreferredDescriptionRoom = 16;

std::string spaces(size_t count) { return std::string(count, ' '); }

// Greedy word wrap. A word longer than the room is emitted whole and may
// overflow: splitting it would corrupt command syntax.
std::vector<std::string> wrap_words(const std::string &text, size_t room) {
  std::vector<std::string> lines;
  if (room == 0) {
    lines.push_back(text);
    return lines;
  }
  std::istringstream iss(text);
  std::string word;
  std::string current;
  while (iss >> word) {
    if (current.empty()) {
      current = word;
    } else if (current.size() + 1 + word.size() <= room) {
      current += " " + word;
    } else {
      lines.push_back(current);
      current = word;
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  if (lines.empty()) {
    lines.push_back("");
  }
  return lines;
}

size_t longest_command() {
  size_t longest = 0;
  for (const auto &section : help_sections()) {
    for (const auto &entry : section.entries) {
      longest = std::max(longest, std::string(entry.command).size());
    }
  }
  return longest;
}

size_t effective_width(int width) {
  return static_cast<size_t>(width < UI::kMinLayoutWidth ? UI::kMinLayoutWidth
                                                         : width);
}

// Joins tokens with a separator, breaking to a continuation indent whenever the
// next token would exceed the width.
std::vector<std::string> join_wrapped(const std::vector<std::string> &tokens,
                                      size_t width, size_t first_sep_size,
                                      const std::string &first_prefix,
                                      const std::string &cont_prefix) {
  std::vector<std::string> lines;
  std::string line = first_prefix;
  bool first = true;
  for (const auto &token : tokens) {
    const std::string sep = first ? spaces(first_sep_size) : " | ";
    if (line.size() + sep.size() + token.size() <= width) {
      line += sep + token;
    } else {
      lines.push_back(line);
      line = cont_prefix + token;
    }
    first = false;
  }
  lines.push_back(line);
  return lines;
}

} // namespace

int UI::layout_width() {
  const int detected = Utils::Platform::terminal_width();
  return detected > 0 ? detected : kFallbackLayoutWidth;
}

std::vector<std::string> UI::help_lines(int width) {
  const size_t w = effective_width(width);
  const size_t indent = std::string(kIndent).size();
  const size_t cont = std::string(kContIndent).size();
  const size_t sep = std::string(kSeparator).size();

  // Column at which descriptions begin. Pulled left when the widest command
  // would leave too little room for a description beside it.
  size_t column = indent + longest_command() + sep;
  if (column + kPreferredDescriptionRoom > w) {
    column = (w > kPreferredDescriptionRoom) ? w - kPreferredDescriptionRoom
                                             : indent;
    if (column < indent) {
      column = indent;
    }
  }

  std::vector<std::string> out;
  out.push_back("Commands");
  out.push_back(std::string(w, '-'));

  bool first_section = true;
  for (const auto &section : help_sections()) {
    if (!first_section) {
      out.push_back("");
    }
    first_section = false;
    out.push_back(section.title);
    for (const auto &entry : section.entries) {
      const std::string cmd_line = std::string(kIndent) + entry.command;
      if (cmd_line.size() < column) {
        // Description starts beside the command and wraps below it. The
        // separator is charged against the room on this line.
        const auto parts = wrap_words(entry.description, w - column - sep);
        std::string line = cmd_line;
        line.append(column - cmd_line.size(), ' ');
        line += std::string(kSeparator) + parts.front();
        out.push_back(line);
        for (size_t i = 1; i < parts.size(); ++i) {
          out.push_back(std::string(kContIndent) + parts[i]);
        }
        continue;
      }
      // Command is too long to share the line. It gets its own wrapped lines,
      // never truncated, then the description at the continuation indent.
      const auto cmd_parts = wrap_words(entry.command, w - cont);
      for (size_t i = 0; i < cmd_parts.size(); ++i) {
        out.push_back(
            (i == 0 ? std::string(kIndent) : std::string(kContIndent)) +
            cmd_parts[i]);
      }
      for (const auto &part : wrap_words(entry.description, w - cont)) {
        out.push_back(std::string(kContIndent) + part);
      }
    }
  }
  out.push_back(std::string(w, '-'));
  return out;
}

std::vector<std::string> UI::status_lines(int width, const std::string &mode,
                                          const std::string &model) {
  const size_t w = effective_width(width);
  const std::vector<std::string> fields = {
      "Mode=" + mode, "Model=" + model, "Memory=Active", "Commands=Available"};
  return join_wrapped(fields, w, 1, "System:", std::string(kContIndent));
}

std::vector<std::string> UI::quick_help_lines(int width) {
  const size_t w = effective_width(width);
  auto out = join_wrapped(quick_help_tokens(), w, 1,
                          "Quick Commands:", std::string(kContIndent));
  out.push_back(std::string(w, '-'));
  return out;
}

// ===== Status =====
void UI::print_enterprise_status() {
  std::cout << Color::BOLD << "Status\n" << Color::RESET;
  print_divider();

  std::cout << Color::GREEN << "Files (4/4)\n" << Color::RESET;
  std::cout << Color::DIM << "   File injection\n" << Color::RESET;
  std::cout << Color::DIM << "   Multi-file operations\n" << Color::RESET;
  std::cout << Color::DIM << "   Context hierarchy\n" << Color::RESET;
  std::cout << Color::DIM << "   Shell integration\n" << Color::RESET;

  std::cout << "\n" << Color::GREEN << "Sessions (4/4)\n" << Color::RESET;
  std::cout << Color::DIM << "   Session management\n" << Color::RESET;
  std::cout << Color::DIM << "   Tool registry\n" << Color::RESET;
  std::cout << Color::DIM << "   Configuration\n" << Color::RESET;
  std::cout << Color::DIM << "   Context compression\n" << Color::RESET;

  std::cout << "\n" << Color::GREEN << "Extensions (4/4)\n" << Color::RESET;
  std::cout << Color::DIM << "   MCP servers\n" << Color::RESET;
  std::cout << Color::DIM << "   Checkpoints\n" << Color::RESET;
  std::cout << Color::DIM << "   Web fetch\n" << Color::RESET;
  std::cout << Color::DIM << "   File filtering\n" << Color::RESET;

  std::cout << "\n" << Color::GREEN << "Security (4/4)\n" << Color::RESET;
  std::cout << Color::DIM << "   Themes\n" << Color::RESET;
  std::cout << Color::DIM << "   Authentication\n" << Color::RESET;
  std::cout << Color::DIM << "   Sandboxing\n" << Color::RESET;
  std::cout << Color::DIM << "   Error handling\n" << Color::RESET;

  std::cout << "\n"
            << Color::BOLD << Color::GREEN << "16 features active\n"
            << Color::RESET;
  print_divider();
}

// ===== Spinner =====
void UI::spinner(const std::string &message, int duration_ms) {
  const std::array<std::string_view, 4> frames = {"/", "-", "\\", "|"};
  const size_t num_frames = frames.size();
  size_t frame = 0;
  auto start = std::chrono::steady_clock::now();

  std::cout << Color::ACCENT << message << " " << Color::RESET;
  while (std::chrono::steady_clock::now() - start <
         std::chrono::milliseconds(duration_ms)) {
    std::cout << "\b" << frames[frame] << std::flush;
    frame = (frame + 1) % num_frames;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "\b" << " " << "\n";
}

// ===== Threaded Spinner =====
void UI::spinner(std::atomic<bool> &done) {
  if (!stdout_is_tty()) {
    // Carriage-return animation is garbage in logs and pipes.
    return;
  }
  const char frames[] = {'|', '/', '-', '\\'};
  int frame = 0;

  while (!done) {
    std::cout << "\r" << frames[frame % 4] << std::flush;
    frame++;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "\r \r" << std::flush;
}

// Threaded spinner with a label. The label carries the meaning (which
// operation is waiting); the frame says it is still alive. The frame count
// and cadence match the unlabeled spinner, so both sizes feel the same.
void UI::spinner(std::atomic<bool> &done, const std::string &label) {
  if (!stdout_is_tty()) {
    // Pipes and logs: one plain line, no animation, no control sequences.
    std::cout << label << "\n";
    return;
  }
  const char frames[] = {'|', '/', '-', '\\'};
  int frame = 0;

  while (!done) {
    std::cout << "\r" << Color::ACCENT << frames[frame % 4] << Color::RESET
              << " " << Color::DIM << label << Color::RESET << "   "
              << std::flush;
    frame++;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "\r\033[K" << std::flush;
}

// ===== Status Messages =====
// Interactive terminals get marker glyphs; pipes and logs keep the plain
// bracket prefixes. The message text is identical either way.
void UI::print_success(const std::string &message) {
  if (colors_enabled()) {
    std::cout << Color::GREEN << "✓ " << Color::RESET << message << "\n";
  } else {
    std::cout << "[OK] " << message << "\n";
  }
}

void UI::print_error(const std::string &message) {
  if (colors_enabled()) {
    std::cout << Color::RED << "✗ " << Color::RESET << message << "\n";
  } else {
    std::cout << "[ERROR] " << message << "\n";
  }
}

void UI::print_warning(const std::string &message) {
  if (colors_enabled()) {
    std::cout << Color::YELLOW << "! " << Color::RESET << message << "\n";
  } else {
    std::cout << "[WARN] " << message << "\n";
  }
}

void UI::print_info(const std::string &message) {
  if (colors_enabled()) {
    std::cout << Color::CYAN << "· " << Color::RESET << message << "\n";
  } else {
    std::cout << "[INFO] " << message << "\n";
  }
}

// ===== Session renderers =====
void UI::print_tool_call(const std::string &tool, const std::string &args) {
  if (!interactive_output()) {
    // Pipes and logs: keep the plain header form so grep finds commands.
    std::cout << "Tool " << tool << "(\"" << args << "\")\n";
    return;
  }
  std::cout << "● " << Color::BOLD << tool << Color::RESET << Color::DIM << "("
            << Color::RESET << args << Color::DIM << ")" << Color::RESET
            << "\n";
}

void UI::print_tool_result(const std::string &result) {
  if (!interactive_output()) {
    std::cout << result << "\n";
    return;
  }
  if (result.empty()) {
    std::cout << "└ " << Color::DIM << "(no output)" << Color::RESET << "\n";
    return;
  }
  // Count the lines so long output carries an honest scale note. The
  // body itself is never hidden; the terminal scrollback is the log.
  size_t line_count = 1;
  for (const char c : result) {
    if (c == '\n') {
      ++line_count;
    }
  }
  if (line_count > 60) {
    std::cout << "└ " << Color::DIM << "[" << line_count
              << " lines of output; scroll to review]" << Color::RESET << "\n";
  }
  std::istringstream in(result);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    // An error line is colored exactly when the payload itself says so;
    // results are otherwise rendered plain, never pre-judged.
    const bool is_error = line.rfind("Error", 0) == 0;
    const bool is_cancel = line.find("cancel") != std::string::npos;
    const std::string marker = first ? "└ " : "  ";
    if (is_error) {
      std::cout << marker << Color::RED << line << Color::RESET << "\n";
    } else if (is_cancel) {
      std::cout << marker << Color::YELLOW << line << Color::RESET << "\n";
    } else {
      std::cout << marker << line << "\n";
    }
    first = false;
  }
}

void UI::print_response(const std::string &response) {
  if (!interactive_output()) {
    std::cout << response << "\n";
    return;
  }
  std::istringstream in(response);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    std::cout << (first ? std::string("● ") : std::string("  ")) << line
              << "\n";
    first = false;
  }
}

// ===== Input frame =====
namespace {
std::string repeat_h(const size_t count) {
  std::string out;
  out.reserve(count * 3);
  for (size_t i = 0; i < count; ++i) {
    out += "\u2500"; // ─
  }
  return out;
}
} // namespace

// ===== Welcome card =====
// The empty-state panel modeled on the reference: a bordered card whose
// title carries the app identity and version, with the session context
// and honest hints inside. Only state the runtime has: version, mode,
// model, cwd, and commands that actually exist.
void UI::print_welcome_panel(const std::string &mode, const std::string &model,
                             const std::string &cwd) {
  if (!interactive_output()) {
    // Pipes and logs: the caller renders the compact legacy banner.
    return;
  }
  const size_t w = static_cast<size_t>(effective_width(layout_width()));
  const size_t box_w =
      std::min<size_t>(w, 88) < 46 ? 46 : std::min<size_t>(w, 88);
  const std::string title = std::string("Agentware v") + Version::get_version();

  auto inner = [&](const std::string &text) {
    std::cout << Color::DIM << "\u2502 " << Color::RESET << text << "\n";
  };

  const std::string context_line =
      "Ready. Mode: " + mode + " | Model: " + model + " | Cwd: " + cwd;
  std::cout << Color::ACCENT << "\u256d\u2500 " << title << " " << Color::RESET
            << Color::DIM
            << repeat_h(box_w > title.size() + 6 ? box_w - title.size() - 6 : 1)
            << "\u256e" << Color::RESET << "\n";
  for (const auto &part : wrap_words(context_line, box_w - 4)) {
    inner(part);
  }
  inner("help: all commands | !<cmd>: run a shell command");
  inner("/chat save|resume <tag>: keep or resume a session");
  std::cout << Color::DIM << "\u2570" << repeat_h(box_w > 2 ? box_w - 2 : 1)
            << "\u256f" << Color::RESET << "\n";
}

void UI::print_user_line(const std::string &user_input) {
  if (!interactive_output()) {
    // Scripts already saw their own input echoed in the log.
    return;
  }
  // Full-width subtle bar behind the request line, like the reference.
  std::cout << "\033[2m\u276f\033[0m "
            << "\033[48;5;236m" << user_input << "\033[K\033[0m"
            << "\n";
}

void UI::print_turn_status(int seconds) {
  const std::string text = "Responded in " + std::to_string(seconds) + "s";
  if (!interactive_output()) {
    std::cout << text << "\n";
    return;
  }
  std::cout << Color::ACCENT << "\u2733" << Color::RESET << Color::DIM << " "
            << text << Color::RESET << "\n";
}

void UI::print_diff_removed(const std::string &text) {
  if (!interactive_output()) {
    std::cout << text << "\n";
    return;
  }
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::cout << Color::RED << "- " << line << Color::RESET << "\n";
  }
}

void UI::print_diff_added(const std::string &text) {
  if (!interactive_output()) {
    std::cout << text << "\n";
    return;
  }
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    std::cout << Color::GREEN << "+ " << line << Color::RESET << "\n";
  }
}

void UI::print_input_frame_top() {
  if (!interactive_output()) {
    return;
  }
  const size_t w = static_cast<size_t>(effective_width(layout_width()));
  std::cout << Color::DIM << "\u256d" << repeat_h(w > 2 ? w - 2 : 1) << "\u256e"
            << Color::RESET << "\n";
}

void UI::print_input_frame_bottom() {
  if (!interactive_output()) {
    return;
  }
  const size_t w = static_cast<size_t>(effective_width(layout_width()));
  std::cout << Color::DIM << "\u2570" << repeat_h(w > 2 ? w - 2 : 1) << "\u256f"
            << Color::RESET << "\n";
}

// ===== Divider =====
void UI::print_divider() {
  std::cout << Color::DIM << "----------------------------------------\n"
            << Color::RESET;
}

// ===== Quick Help Interface =====
std::vector<std::string> UI::prompt_lines(int width,
                                          const std::string &prompt) {
  const size_t w = effective_width(width);
  const size_t cont = std::string(kContIndent).size();
  // Trailing space separates the prompt from the cursor, so it is not part of
  // the wrapped body and is re-appended by the caller.
  size_t trailing = 0;
  while (trailing < prompt.size() &&
         std::isspace(static_cast<unsigned char>(
             prompt[prompt.size() - 1 - trailing]))) {
    ++trailing;
  }
  const std::string body = prompt.substr(0, prompt.size() - trailing);
  return wrap_words(body, w - cont);
}

void UI::print_wrapped_prompt(const std::string &prompt) {
  const auto parts = prompt_lines(layout_width(), prompt);
  size_t trailing = 0;
  while (trailing < prompt.size() &&
         std::isspace(static_cast<unsigned char>(
             prompt[prompt.size() - 1 - trailing]))) {
    ++trailing;
  }
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      std::cout << "\n" << kContIndent;
    }
    std::cout << parts[i];
  }
  std::cout << prompt.substr(prompt.size() - trailing);
}

namespace {
// Colors one already-laid-out help line. Builders return plain text so their
// widths can be asserted directly; color is applied only at emission.
std::string colorize_help_line(const std::string &line) {
  if (line.empty()) {
    return line;
  }
  if (line == "Commands") {
    return std::string(Color::BOLD) + line + Color::RESET;
  }
  if (line.find_first_not_of('-') == std::string::npos) {
    return std::string(Color::DIM) + line + Color::RESET;
  }
  const std::string cont = kContIndent;
  if (line.compare(0, cont.size(), cont) == 0) {
    return line; // Description continuation stays plain.
  }
  const std::string ind = kIndent;
  if (line.compare(0, ind.size(), ind) == 0) {
    const size_t sep = line.find(kSeparator);
    if (sep != std::string::npos) {
      return std::string(Color::CYAN) + line.substr(0, sep) + Color::RESET +
             line.substr(sep);
    }
    return std::string(Color::CYAN) + line + Color::RESET;
  }
  return std::string(Color::GREEN) + line + Color::RESET;
}
} // namespace

void UI::print_help() {
  for (const auto &line : help_lines(layout_width())) {
    std::cout << colorize_help_line(line) << "\n";
  }
}

void UI::print_quick_help() {
  const std::string prefix = "Quick Commands:";
  for (const auto &line : quick_help_lines(layout_width())) {
    if (line.find_first_not_of('-') == std::string::npos) {
      std::cout << Color::DIM << line << Color::RESET << "\n";
      continue;
    }
    if (line.compare(0, prefix.size(), prefix) == 0) {
      std::cout << Color::DIM << prefix << Color::RESET << Color::CYAN
                << line.substr(prefix.size()) << Color::RESET << "\n";
      continue;
    }
    std::cout << Color::CYAN << line << Color::RESET << "\n";
  }
}

// ===== System Info =====
void UI::print_system_info(const std::string &mode, const std::string &model) {
  const std::string prefix = "System:";
  for (const auto &line : status_lines(layout_width(), mode, model)) {
    if (line.compare(0, prefix.size(), prefix) == 0) {
      std::cout << Color::DIM << prefix << Color::RESET << Color::GREEN
                << line.substr(prefix.size()) << Color::RESET << "\n";
      continue;
    }
    std::cout << Color::GREEN << line << Color::RESET << "\n";
  }
}

// ===== Ready Interface =====
void UI::print_ready_interface(const std::string &mode,
                               const std::string &model) {
  print_system_info(mode, model);
  print_quick_help();
  // Wrapped like every other startup line, so it cannot overflow a narrow
  // terminal. One line at width 40, so it costs no vertical budget.
  for (const auto &part :
       prompt_lines(layout_width(), "Ready - Type a command or just chat:")) {
    std::cout << part << "\n";
  }
}

// ===== Prompted Input =====
std::string UI::prompt_user(const std::string &prompt_text) {
  // Only print the prompt once with consistent formatting
  std::cout << Color::BOLD << "[You] " << prompt_text << Color::RESET << " > ";

  std::string input;
  std::getline(std::cin, input);
  return input;
}

} // namespace Utils
