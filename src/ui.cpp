#include "ui.h"
// After ui.h: that header includes config.h, which defines NOMINMAX on
// Windows. platform.h includes <windows.h>, and without NOMINMAX already
// defined, windef.h defines min/max as macros and std::max stops parsing.
#include "platform.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
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
} // namespace Color

// ===== Logo =====
void UI::print_logo() {
  static const std::array<std::string_view, 8> kLogo = {
      "██         █",     "██  █  ██  ██",    "██ ██  ██  ███",
      "██ ██  ██   ██",   "██ ██  ██   ███",  "██ ██  ██    ██",
      "██ ██  ██    ███", "██ ██  ██     ██",
  };

  std::cout << Color::PINK;
  for (const auto &line : kLogo) {
    std::cout << line << "\n";
  }
  std::cout << Color::RESET;

  std::cout << Color::DIM << "Agentware\n" << Color::RESET;

  std::cout << Color::DIM << "Welcome to Coccinella Labs\n" << Color::RESET;

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

  std::cout << Color::CYAN << message << " " << Color::RESET;
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

// ===== Status Messages =====
void UI::print_success(const std::string &message) {
  std::cout << Color::GREEN << "[OK] " << Color::RESET << message << "\n";
}

void UI::print_error(const std::string &message) {
  std::cout << Color::RED << "[ERROR] " << Color::RESET << message << "\n";
}

void UI::print_warning(const std::string &message) {
  std::cout << Color::YELLOW << "[WARN] " << Color::RESET << message << "\n";
}

void UI::print_info(const std::string &message) {
  std::cout << Color::CYAN << "[INFO] " << Color::RESET << message << "\n";
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
