#include "ui.h"
#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Utils {

// ===== Color Codes =====
namespace Color {
// Exported color constants
AGENTWARE_API const std::string RESET = "\033[0m";
AGENTWARE_API const std::string GREEN = "\033[32m";
AGENTWARE_API const std::string YELLOW = "\033[33m";
AGENTWARE_API const std::string RED = "\033[31m";
AGENTWARE_API const std::string CYAN = "\033[36m";
AGENTWARE_API const std::string BOLD = "\033[1m";
AGENTWARE_API const std::string DIM = "\033[2m";
// Logo color (matches Coccinella Labs pink #F34B7D)
AGENTWARE_API const std::string PINK = "\033[38;2;243;75;125m";
} // namespace Color

// ===== Logo =====
void UI::print_logo() {
  static const std::array<std::string_view, 8> kLogo = {
      "██         █",
      "██  █  ██  ██",
      "██ ██  ██  ███",
      "██ ██  ██   ██",
      "██ ██  ██   ███",
      "██ ██  ██    ██",
      "██ ██  ██    ███",
      "██ ██  ██     ██",
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
void UI::print_help() {
  std::cout << Color::BOLD << "Commands\n" << Color::RESET;
  print_divider();

  std::cout << Color::GREEN << "Files\n" << Color::RESET;
  std::cout << Color::CYAN << "  @<path> <prompt>" << Color::RESET
            << "        - Inject a file or directory into chat\n";
  std::cout << Color::CYAN << "  read:<file>[:start[:count]]" << Color::RESET
            << " - Read a file or range\n";
  std::cout << Color::CYAN << "  write:<file> <content>" << Color::RESET
            << "   - Write a file\n";
  std::cout << Color::CYAN << "  replace:<file>:<old>:<new>[:count]"
            << Color::RESET << " - Replace text\n";
  std::cout << Color::CYAN << "  grep:<pattern>[:dir[:filter]]" << Color::RESET
            << " - Search text\n";
  std::cout << Color::CYAN << "  /files <path...> [options]" << Color::RESET
            << " - Read multiple files\n";

  std::cout << "\n" << Color::GREEN << "Sessions\n" << Color::RESET;
  std::cout << Color::CYAN << "  /chat save|resume <tag>" << Color::RESET
            << "    - Save or resume a conversation\n";
  std::cout << Color::CYAN << "  /chat list" << Color::RESET
            << "                 - List saved conversations\n";
  std::cout << Color::CYAN << "  /compress" << Color::RESET
            << "               - Compress context\n";
  std::cout << Color::CYAN << "  remember:<fact>" << Color::RESET
            << "          - Remember fact\n";
  std::cout << Color::CYAN << "  memory | forget | clear" << Color::RESET
            << "      - Show or clear memory\n";
  std::cout << Color::CYAN << "  /memory show|add <text>" << Color::RESET
            << "  - Inspect or add memory\n";
  std::cout << Color::CYAN << "  /context show|refresh|create" << Color::RESET
            << " - Manage project context\n";

  std::cout << "\n" << Color::GREEN << "Project\n" << Color::RESET;
  std::cout << Color::CYAN << "  analyze: | components: | todos: | tree:"
            << Color::RESET << " - Inspect the project\n";
  std::cout << Color::CYAN << "  git:status | git:log | git:analyze"
            << Color::RESET << " - Inspect Git\n";

  std::cout << "\n" << Color::GREEN << "Web and GitHub\n" << Color::RESET;
  std::cout << Color::CYAN << "  /fetch <url> [text|json|raw]" << Color::RESET
            << " - Fetch content\n";
  std::cout << Color::CYAN << "  search:<query>" << Color::RESET
            << "            - Search the web\n";
  std::cout << Color::CYAN << "  github:repo:<owner>/<repo>" << Color::RESET
            << "  - Show repository details\n";
  std::cout << Color::CYAN << "  github:issues:<owner>/<repo>" << Color::RESET
            << " - List issues\n";
  std::cout << Color::CYAN << "  github:health:<owner>/<repo>" << Color::RESET
            << " - Run repository health checks\n";
  std::cout << Color::CYAN << "  github:threads:<owner>/<repo>:<pr>[:query]"
            << Color::RESET << " - List review threads\n";
  std::cout << Color::CYAN << "  /mcp <command>" << Color::RESET
            << "             - Inspect MCP configuration\n";

  std::cout << "\n" << Color::GREEN << "Checkpoints\n" << Color::RESET;
  std::cout << Color::CYAN << "  /checkpoint <command>" << Color::RESET
            << "      - Create, list, inspect, or delete checkpoints\n";
  std::cout << Color::CYAN << "  /restore [id]" << Color::RESET
            << "               - List or restore a checkpoint\n";

  std::cout << "\n" << Color::GREEN << "Settings and diagnostics\n"
            << Color::RESET;
  std::cout << Color::CYAN << "  /theme <command>" << Color::RESET
            << "           - Manage themes\n";
  std::cout << Color::CYAN << "  /auth <command>" << Color::RESET
            << "            - Manage provider credentials\n";
  std::cout << Color::CYAN << "  /sandbox <command>" << Color::RESET
            << "         - Manage or run sandboxes\n";
  std::cout << Color::CYAN << "  /error <command>" << Color::RESET
            << "           - Inspect diagnostics\n";
  std::cout << Color::CYAN << "  /stats" << Color::RESET
            << "                     - Show session statistics\n";

  std::cout << "\n" << Color::GREEN << "System\n" << Color::RESET;
  std::cout << Color::CYAN << "  cmd:<command>" << Color::RESET
            << "             - Run a command\n";
  std::cout << Color::CYAN << "  !<command>" << Color::RESET
            << "              - Run a shell command\n";
  std::cout << Color::CYAN << "  /tools" << Color::RESET
            << "                   - Show direct tools\n";
  std::cout << Color::CYAN << "  help | /help | /?" << Color::RESET
            << "        - Show command help\n";
  std::cout << Color::CYAN << "  version | exit | quit" << Color::RESET
            << "        - Show version or leave\n";
  print_divider();
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
void UI::print_quick_help() {
  std::cout << Color::DIM << "Quick Commands: " << Color::RESET;
  std::cout << Color::CYAN << "help" << Color::RESET << " | ";

  std::cout << Color::CYAN << "search:query" << Color::RESET << " | ";
  std::cout << Color::CYAN << "cmd:command" << Color::RESET << " | ";
  std::cout << Color::CYAN << "read:file" << Color::RESET << " | ";
  std::cout << Color::CYAN << "quit" << Color::RESET << "\n";

  std::cout << Color::CYAN << "version" << Color::RESET << " | ";
  std::cout << Color::CYAN << "search:query" << Color::RESET << " | ";
  std::cout << Color::CYAN << "cmd:command" << Color::RESET << " | ";
  std::cout << Color::CYAN << "read:file" << Color::RESET << " | ";
  std::cout << Color::CYAN << "write:file text" << Color::RESET << " | ";
  std::cout << Color::CYAN << "exit" << Color::RESET << "\n";

  print_divider();
}

// ===== System Info =====
void UI::print_system_info(const std::string &mode, const std::string &model) {
  std::cout << Color::DIM << "System: " << Color::RESET;
  std::cout << Color::GREEN << "Mode=" << mode << Color::RESET << " | ";
  std::cout << Color::GREEN << "Model=" << model << Color::RESET << " | ";
  std::cout << Color::GREEN << "Memory=Active" << Color::RESET << " | ";
  std::cout << Color::GREEN << "Commands=Available" << Color::RESET << "\n";
}

// ===== Ready Interface =====
void UI::print_ready_interface(const std::string &mode,
                               const std::string &model) {
  print_system_info(mode, model);
  print_quick_help();
  std::cout << "Ready - Type a command or chat naturally:\n";
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
