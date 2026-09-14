#include "agent.h"
#include "memory_manager.h"
#include "ai_service.h"
#include "auth_service.h"
#include "checkpoint_service.h"
#include "codebase_service.h"
#include "command_service.h"
#include "context_service.h"
#include "error_service.h"
#include "file_service.h"
#include "git_service.h"
#include "github_service.h"
#include "mcp_service.h"
#include "multi_file_service.h"
#include "sandbox_service.h"
#include "theme_service.h"
#include "web_service.h"
#include "config.h"
#include "ui.h"
#include "validation.h"
#include "version.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <thread>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

// Constants for response handling
const size_t MAX_RESPONSE_LENGTH = 8000;

// Using the Mode enum from agent.h instead of separate constants

namespace Core {
Agent::Agent()
    : memory_(std::make_unique<Data::MemoryManager>()), ai_service_(nullptr) {}

Agent::~Agent() {
  // Destructor defined here because unique_ptr types are forward declared
}

// Helper: trim whitespace
static inline std::string trim_copy(const std::string &s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

void Agent::run() {
  initialize_mode();

  // Data-driven model name mapping
  // Data-driven model name mapping
  std::map<Agent::Mode, std::string> model_names = {
      {Agent::Mode::MODE_TOGETHER, "Together AI"},
      {Agent::Mode::MODE_CEREBRAS, "Cerebras"},
      {Agent::Mode::MODE_FIREWORKS, "Fireworks"},
      {Agent::Mode::MODE_GROQ, "Groq"},
      {Agent::Mode::MODE_DEEPSEEK, "DeepSeek"},
      {Agent::Mode::MODE_OPENAI, "OpenAI"},
      {Agent::Mode::MODE_LLAMA_3B, "llama3.2:3b"},
      {Agent::Mode::MODE_LLAMA_LATEST, "llama3.2:latest"},
      {Agent::Mode::MODE_LLAMA_31, "llama3.1:latest"}};

  // Show enhanced ready interface with system info and quick help
  std::string mode_name = is_online_mode() ? "Online" : "Offline";
  std::string model_name =
      model_names.count(mode_) ? model_names[mode_] : "Unknown";
  Utils::UI::print_ready_interface(mode_name, model_name);

  while (true) {
    std::cout << "> ";
    std::string user_input;
    if (!std::getline(std::cin, user_input)) {
      break; // EOF or error
    }

    user_input = trim_copy(user_input);
    if (user_input.empty())
      continue;

    if (user_input == "exit" || user_input == "quit")
      break;
    if (user_input == "help") {
      Utils::UI::print_help();
      continue;
    }
    if (user_input == "version") {
      Version::print_version_info();
      continue;
    }

    process_user_input(user_input);
  }

  std::cout << "Goodbye\n";
}

void Agent::initialize_mode() {
  // Data-driven provider configuration
  struct ProviderInfo {
    int choice;
    Mode mode;
    std::string env_var;
    std::string display_name;
  };

  std::vector<ProviderInfo> providers = {
      {1, Agent::Mode::MODE_TOGETHER, "TOGETHER_API_KEY", "Together AI"},
      {2, Agent::Mode::MODE_CEREBRAS, "CEREBRAS_API_KEY", "Cerebras"},
      {3, Agent::Mode::MODE_FIREWORKS, "FIREWORKS_API_KEY", "Fireworks"},
      {4, Agent::Mode::MODE_GROQ, "GROQ_API_KEY", "Groq"},
      {5, Agent::Mode::MODE_DEEPSEEK, "DEEPSEEK_API_KEY", "DeepSeek"},
      {6, Agent::Mode::MODE_OPENAI, "OPENAI_API_KEY", "OpenAI"}};

  // Simplified mode selection with consistent numbering
  int choice =
      get_user_choice("Mode [1=Online / 2=Offline] (default 2): ", {1, 2}, 2);

  if (choice == 1) {
    // Online mode: pick provider
    std::string prompt = "Provider [";
    for (size_t i = 0; i < providers.size(); ++i) {
      if (i > 0)
        prompt += " / ";
      prompt +=
          std::to_string(providers[i].choice) + "=" + providers[i].display_name;
    }
    prompt += "] (default 1): ";

    std::vector<int> valid_choices;
    for (const auto &p : providers)
      valid_choices.push_back(p.choice);

    int provider_choice = get_user_choice(prompt, valid_choices, 1);

    // Find and configure the selected provider
    for (const auto &provider : providers) {
      if (provider.choice == provider_choice) {
        mode_ = provider.mode;
        api_key_ = Utils::Config::get_env_var(provider.env_var);
        if (api_key_.empty()) {
          throw std::runtime_error(provider.env_var + " not set");
        }
        Utils::UI::print_success(provider.display_name);
        break;
      }
    }
  } else {
    // Data-driven offline model configuration
    struct ModelInfo {
      int choice;
      Mode mode;
      std::string display_name;
    };

    std::vector<ModelInfo> models = {
        {1, Agent::Mode::MODE_LLAMA_3B, "llama3.2:3b"},
        {2, Agent::Mode::MODE_LLAMA_LATEST, "llama3.2:latest"},
        {3, Agent::Mode::MODE_LLAMA_31, "llama3.1:latest"}};

    // Offline mode: pick model
    std::string prompt = "Model [";
    for (size_t i = 0; i < models.size(); ++i) {
      if (i > 0)
        prompt += " / ";
      prompt += std::to_string(models[i].choice) + "=" + models[i].display_name;
    }
    prompt += "] (default 1): ";

    std::vector<int> valid_choices;
    for (const auto &m : models)
      valid_choices.push_back(m.choice);

    int model_choice = get_user_choice(prompt, valid_choices, 1);

    // Find and configure the selected model
    for (const auto &model : models) {
      if (model.choice == model_choice) {
        mode_ = model.mode;
        Utils::UI::print_success(model.display_name);
        break;
      }
    }
  }
}

bool Agent::is_online_mode() const {
  return mode_ == Agent::Mode::MODE_TOGETHER ||
         mode_ == Agent::Mode::MODE_CEREBRAS ||
         mode_ == Agent::Mode::MODE_FIREWORKS ||
         mode_ == Agent::Mode::MODE_GROQ ||
         mode_ == Agent::Mode::MODE_DEEPSEEK ||
         mode_ == Agent::Mode::MODE_OPENAI;
}

int Agent::get_user_choice(const std::string &prompt,
                           const std::vector<int> &valid_choices,
                           int default_choice) {
  while (true) {
    std::cout << prompt;
    std::string input;
    if (!std::getline(std::cin, input)) {
      // EOF -> return default
      return default_choice;
    }

    input = trim_copy(input);

    // Accept empty line as confirmation of default choice
    if (input.empty())
      return default_choice;

    try {
      int choice = std::stoi(input);
      if (std::find(valid_choices.begin(), valid_choices.end(), choice) !=
          valid_choices.end()) {
        return choice;
      }
    } catch (const std::exception &) {
      // fallthrough -> invalid input, prompt again
    }
    Utils::UI::print_warning("Invalid choice, please try again.");
  }
}

void Agent::process_user_input(const std::string &input) {
  command_count_++;
  std::string trimmed_input = trim_copy(input);

  // Handle @ file injection commands
  if (trimmed_input.find('@') != std::string::npos) {
    handle_file_injection_command(trimmed_input);
    return;
  }

  // Handle ! shell commands
  if (trimmed_input.rfind("!", 0) == 0) {
    handle_shell_command(trimmed_input);
    return;
  }

  // Handle / meta commands
  if (trimmed_input.rfind("/", 0) == 0) {
    handle_meta_command(trimmed_input);
    return;
  }

  // Check for direct commands (detect colon)
  if (trimmed_input.find(':') != std::string::npos) {
    handle_direct_command(trimmed_input);
  } else {
    handle_ai_chat(trimmed_input);
  }
}

void Agent::handle_direct_command(const std::string &input) {
  std::string result;

  if (input.rfind("search:", 0) == 0) {
    std::string query = trim_copy(input.substr(7));
    auto validation = Utils::Validator::validate_search_query(query);
    if (!validation.is_valid) {
      result = "Error: " + validation.error_message;
    } else {
      if (!validation.warnings.empty()) {
        for (const auto &warning : validation.warnings) {
          Utils::UI::print_warning(warning);
        }
      }
      result = Services::WebService::search(query);
      memory_->save_interaction("search:" + query, result);
    }
  } else if (input.rfind("cmd:", 0) == 0) {
    std::string command = trim_copy(input.substr(4));
    auto validation = Utils::Validator::validate_command_safe(command);
    if (!validation.warnings.empty()) {
      for (const auto &warning : validation.warnings) {
        Utils::UI::print_warning(warning);
      }
      std::cout << "Continue? (y/N): ";
      std::string confirm;
      std::getline(std::cin, confirm);
      if (confirm != "y" && confirm != "Y") {
        result = "Command cancelled by user";
        memory_->save_interaction("cmd:" + command, result);
        std::cout << result << std::endl;
        return;
      }
    }
    result = Services::CommandService::execute(command);
    memory_->save_interaction("cmd:" + command, result);
  } else if (input.rfind("read:", 0) == 0) {
    std::string params = trim_copy(input.substr(5));
    // Check if it has range parameters: read:filename:start:count
    size_t first_colon = params.find(':');
    if (first_colon != std::string::npos) {
      std::string filename = params.substr(0, first_colon);
      auto validation = Utils::Validator::validate_file_exists(filename);
      if (!validation.is_valid) {
        result = "Error: " + validation.error_message;
      } else {
        std::string range_params = params.substr(first_colon + 1);
        size_t second_colon = range_params.find(':');

        if (second_colon != std::string::npos) {
          int start_line = std::stoi(range_params.substr(0, second_colon));
          int line_count = std::stoi(range_params.substr(second_colon + 1));
          result = Services::FileService::read_file_range(filename, start_line,
                                                          line_count);
        } else {
          int start_line = std::stoi(range_params);
          result = Services::FileService::read_file_range(filename, start_line);
        }
      }
    } else {
      auto validation = Utils::Validator::validate_file_exists(params);
      if (!validation.is_valid) {
        result = "Error: " + validation.error_message;
      } else {
        result = Services::FileService::read_file(params);
      }
    }
    memory_->save_interaction("read:" + params, result);
  } else if (input.rfind("write:", 0) == 0) {
    // Format: write:filename content...
    size_t space_pos = input.find(' ', 6); // index 6 is safe for "write:"
    if (space_pos != std::string::npos) {
      std::string filename = trim_copy(input.substr(6, space_pos - 6));
      std::string content = input.substr(space_pos + 1);

      // Validate the file is writable
      auto validation = Utils::Validator::validate_file_writable(filename);
      if (!validation.is_valid) {
        result = "Error: " + validation.error_message;
      } else {
        result = Services::FileService::write_file(filename, content);
        memory_->save_interaction("write:" + filename, result);
      }
    } else {
      result = "Usage: write:filename content";
    }
  } else if (input.rfind("replace:", 0) == 0) {
    // Format: replace:filename:old_text:new_text[:expected_count]
    std::string params = input.substr(8);
    std::vector<std::string> parts;
    size_t pos = 0;
    size_t colon_pos;

    // Split by colons (up to 4 parts)
    while ((colon_pos = params.find(':', pos)) != std::string::npos &&
           parts.size() < 3) {
      parts.push_back(params.substr(pos, colon_pos - pos));
      pos = colon_pos + 1;
    }
    parts.push_back(params.substr(pos)); // Last part

    if (parts.size() >= 3) {
      const std::string &filename = parts[0];
      const std::string &old_text = parts[1];
      const std::string &new_text = parts[2];
      int expected = (parts.size() > 3) ? std::stoi(parts[3]) : 1;

      auto edit_result = Services::FileService::replace_text_in_file(
          filename, old_text, new_text, expected);
      result = edit_result.message;
      memory_->save_interaction("replace:" + filename, result);
    } else {
      result = "Usage: replace:filename:old_text:new_text[:expected_count]";
    }
  } else if (input.rfind("grep:", 0) == 0) {
    // Format: grep:pattern[:directory[:file_filter]]
    std::string params = input.substr(5);
    std::vector<std::string> parts;
    size_t pos = 0;
    size_t colon_pos;

    while ((colon_pos = params.find(':', pos)) != std::string::npos &&
           parts.size() < 2) {
      parts.push_back(params.substr(pos, colon_pos - pos));
      pos = colon_pos + 1;
    }
    parts.push_back(params.substr(pos));

    std::string pattern = parts[0];
    std::string directory = (parts.size() > 1) ? parts[1] : ".";
    std::string filter = (parts.size() > 2) ? parts[2] : "*";

    auto search_results =
        Services::FileService::search_in_directory(directory, pattern, filter);

    if (search_results.empty()) {
      result = "No matches found for pattern: " + pattern;
    } else {
      result = "Found " + std::to_string(search_results.size()) + " matches:\n";
      for (const auto &match : search_results) {
        result += match.file_path + ":" + std::to_string(match.line_number) +
                  ": " + match.line_content + "\n";
      }
    }
    memory_->save_interaction("grep:" + pattern, result);
  } else if (input.rfind("remember:", 0) == 0) {
    std::string fact = trim_copy(input.substr(9));
    memory_->save_global_fact(fact);
    result = "Remembered: " + fact;
  } else if (input.rfind("forget", 0) == 0) {
    memory_->clear_global_memory();
    result = "Global memory cleared";
  } else if (input.rfind("memory", 0) == 0) {
    std::string global_context = memory_->get_global_context();
    result =
        global_context.empty() ? "No global memories stored" : global_context;
  } else if (input.rfind("clear", 0) == 0) {
    memory_->clear_memory();
    result = "Session memory cleared";
  } else if (input.rfind("analyze:", 0) == 0) {
    std::string path = trim_copy(input.substr(8));
    if (path.empty()) {
      path = "."; // Current directory
    }
    result = Services::CodebaseService::analyze_structure(path);
    memory_->save_interaction("analyze:" + path, result);
  } else if (input.rfind("components:", 0) == 0) {
    std::string path = trim_copy(input.substr(11));
    if (path.empty()) {
      path = "."; // Current directory
    }
    result = Services::CodebaseService::find_main_components(path);
    memory_->save_interaction("components:" + path, result);
  } else if (input.rfind("todos:", 0) == 0) {
    std::string path = trim_copy(input.substr(6));
    if (path.empty()) {
      path = "."; // Current directory
    }
    auto todos = Services::CodebaseService::find_todos(path);
    if (todos.empty()) {
      result = "No task comments found";
    } else {
      result = "Found " + std::to_string(todos.size()) + " task comments:\n";
      for (const auto &todo : todos) {
        result += todo + "\n";
      }
    }
    memory_->save_interaction("todos:" + path, result);
  } else if (input.rfind("git:", 0) == 0) {
    std::string params = trim_copy(input.substr(4));
    std::string path = "."; // Default to current directory

    if (params.find("log") == 0) {
      result = Services::GitService::get_git_log(path, 7);
    } else if (params.find("status") == 0) {
      result = Services::GitService::get_git_status(path);
    } else if (params.find("analyze") == 0) {
      result = Services::GitService::analyze_repository(path);
    } else {
      result = "Usage: git:log, git:status, git:analyze";
    }
    memory_->save_interaction("git:" + params, result);
  } else if (input.rfind("tree:", 0) == 0) {
    std::string path = trim_copy(input.substr(5));
    if (path.empty()) {
      path = "."; // Current directory
    }
    result = Services::CodebaseService::get_directory_tree(path, 3);
    memory_->save_interaction("tree:" + path, result);
  } else if (input.rfind("github:", 0) == 0) {
    std::string params = trim_copy(input.substr(7));
    auto parse_repo_spec = [](const std::string &repo_spec, std::string &owner,
                              std::string &repo) -> bool {
      size_t slash_pos = repo_spec.find('/');
      if (slash_pos != std::string::npos) {
        owner = repo_spec.substr(0, slash_pos);
        repo = repo_spec.substr(slash_pos + 1);
        return true;
      }
      return false;
    };
    if (params.rfind("repo:", 0) == 0) {
      std::string repo_spec = trim_copy(params.substr(5));
      std::string owner, repo;
      if (parse_repo_spec(repo_spec, owner, repo)) {
        result = Services::GitHubService::get_repo_info(owner, repo);
      } else {
        result = "Usage: github:repo:owner/repo";
      }
    } else if (params.rfind("issues:", 0) == 0) {
      std::string repo_spec = trim_copy(params.substr(7));
      std::string owner, repo;
      if (parse_repo_spec(repo_spec, owner, repo)) {
        auto issues = Services::GitHubService::get_issues(owner, repo);
        if (issues.empty()) {
          result = "No issues found";
        } else {
          result = "Found " + std::to_string(issues.size()) + " issues:\n";
          for (const auto &issue : issues) {
            result +=
                "#" + std::to_string(issue.number) + ": " + issue.title + "\n";
          }
        }
      } else {
        result = "Usage: github:issues:owner/repo";
      }
    } else if (params.rfind("health:", 0) == 0) {
      std::string repo_spec = trim_copy(params.substr(7));
      std::string owner, repo;
      if (parse_repo_spec(repo_spec, owner, repo)) {
        result = Services::GitHubService::run_health_check(owner, repo);
      } else {
        result = "Usage: github:health:owner/repo";
      }
    } else {
      result = "Usage: github:repo:owner/repo, github:issues:owner/repo, "
               "github:health:owner/repo";
    }
    memory_->save_interaction("github:" + params, result);
  } else {
    result = "Unknown command";
  }

  if (!result.empty()) {
    std::cout << result << std::endl;
  }
}

void Agent::handle_ai_chat(const std::string &input) {
  if (!ai_service_) {
    ai_service_ = std::make_unique<Services::AIService>(mode_, api_key_);
  }

  if (!ai_service_->is_available()) {
    std::cout << "AI service unavailable\n";
    return;
  }

  std::atomic<bool> done(false);
  std::thread spin([&done]() { Utils::UI::spinner(done); });

  // Build enhanced context with hierarchical context
  std::string memory_context = memory_->get_context_string();
  std::string hierarchical_context =
      Services::ContextService::load_hierarchical_context(".");

  std::string full_context = memory_context;
  if (!hierarchical_context.empty()) {
    full_context = hierarchical_context + "\n\n" + memory_context;
  }

  std::string response = ai_service_->chat(input, full_context);

  done = true;
  if (spin.joinable())
    spin.join();

  if (!response.empty()) {
    std::cout << response << std::endl;
    memory_->save_interaction(input, response);
  } else {
    std::cout << "No response\n";
  }
}

void Agent::handle_file_injection_command(const std::string &input) {
  // Process @ file injections and then send to AI
  std::string processed_input = process_file_injections(input);
  handle_ai_chat(processed_input);
}

void Agent::handle_shell_command(const std::string &input) {
  if (input == "!") {
    // Toggle shell mode
    toggle_shell_mode();
    return;
  }

  // Execute shell command
  std::string command = trim_copy(input.substr(1));
  if (command.empty()) {
    std::cout << "Usage: !<command> or ! to toggle shell mode" << std::endl;
    return;
  }

  auto validation = Utils::Validator::validate_command_safe(command);
  if (!validation.warnings.empty()) {
    for (const auto &warning : validation.warnings) {
      Utils::UI::print_warning(warning);
    }
    std::cout << "Continue? (y/N): ";
    std::string confirm;
    std::getline(std::cin, confirm);
    if (confirm != "y" && confirm != "Y") {
      std::cout << "Command cancelled by user" << std::endl;
      return;
    }
  }

  std::string result = Services::CommandService::execute(command);
  std::cout << result << std::endl;
  memory_->save_interaction("!" + command, result);
}

void Agent::handle_meta_command(const std::string &input) {
  std::string command = trim_copy(input.substr(1));

  if (command == "help" || command == "?") {
    show_meta_help();
  } else if (command == "clear") {
    clear_screen();
  } else if (command.rfind("chat ", 0) == 0) {
    handle_chat_management(command.substr(5));
  } else if (command == "tools") {
    show_available_tools();
  } else if (command == "memory show") {
    show_memory_context();
  } else if (command.rfind("memory add ", 0) == 0) {
    add_to_memory(command.substr(11));
  } else if (command == "compress") {
    compress_context();
  } else if (command == "stats") {
    show_session_stats();
  } else if (command.rfind("context ", 0) == 0) {
    handle_context_management(command.substr(8));
  } else if (command.rfind("files ", 0) == 0) {
    handle_multi_file_command(command.substr(6));
  } else if (command.rfind("fetch ", 0) == 0) {
    handle_web_fetch_command(command.substr(6));
  } else if (command.rfind("checkpoint ", 0) == 0) {
    handle_checkpoint_command(command.substr(11));
  } else if (command == "restore") {
    handle_checkpoint_command("list");
  } else if (command.rfind("restore ", 0) == 0) {
    handle_checkpoint_command("restore " + command.substr(8));
  } else if (command.rfind("mcp ", 0) == 0) {
    handle_mcp_command(command.substr(4));
  } else if (command.rfind("theme ", 0) == 0) {
    handle_theme_command(command.substr(6));
  } else if (command.rfind("auth ", 0) == 0) {
    handle_auth_command(command.substr(5));
  } else if (command.rfind("sandbox ", 0) == 0) {
    handle_sandbox_command(command.substr(8));
  } else if (command.rfind("error ", 0) == 0) {
    handle_error_command(command.substr(6));
  } else if (command == "quit" || command == "exit") {
    std::cout << "Goodbye!" << std::endl;
    exit(0);
  } else {
    std::cout << "Unknown meta command: /" << command << std::endl;
    std::cout << "Type /help for available commands." << std::endl;
  }
}

std::string Agent::process_file_injections(const std::string &input) {
  std::string result = input;
  size_t pos = 0;

  // Find all @ symbols and process file paths
  while ((pos = result.find('@', pos)) != std::string::npos) {
    // Skip if it's escaped or part of an email
    if (pos > 0 && result[pos - 1] == '\\') {
      pos++;
      continue;
    }

    // Find the end of the path (space, newline, or end of string)
    size_t start = pos + 1;
    size_t end = start;

    // Handle quoted paths
    bool quoted = false;
    if (start < result.length() && result[start] == '"') {
      quoted = true;
      start++;
      end = result.find('"', start);
      if (end == std::string::npos)
        end = result.length();
    } else {
      // Find end of unquoted path
      while (end < result.length() && result[end] != ' ' &&
             result[end] != '\n' && result[end] != '\t' &&
             result[end] != '\r') {
        end++;
      }
    }

    if (start < end) {
      std::string path = result.substr(start, end - start);
      std::string file_content = read_file_or_directory(path);

      // Replace @path with file content
      size_t replace_start = pos;
      size_t replace_end = quoted ? end + 1 : end;
      result.replace(replace_start, replace_end - replace_start, file_content);

      pos = replace_start + file_content.length();
    } else {
      pos++;
    }
  }

  return result;
}

std::string Agent::read_file_or_directory(const std::string &path) {
  try {
    if (!std::filesystem::exists(path)) {
      return "[File not found: " + path + "]";
    }

    if (std::filesystem::is_directory(path)) {
      // Use multi-file service for directory reading
      Services::MultiFileOptions options;
      options.max_files = 50; // Reasonable limit for @ injection
      options.max_total_size = 5 * 1024 * 1024; // 5MB limit

      auto files =
          Services::MultiFileService::read_directory_files(path, options);

      if (files.empty()) {
        return "[No readable files found in directory: " + path + "]";
      }

      return Services::MultiFileService::format_multi_file_content(
          files, "Contents of directory: " + path);
    } else {
      // Read single file
      std::ifstream file(path);
      if (!file.is_open()) {
        return "[Could not read file: " + path + "]";
      }

      std::ostringstream content;
      content << "[Contents of file: " << path << "]\n\n";

      std::string line;
      int line_count = 0;
      while (std::getline(file, line) && line_count < 1000) {
        content << line << "\n";
        line_count++;
      }

      if (line_count >= 1000) {
        content << "\n[File truncated - too long]";
      }

      return content.str();
    }
  } catch (const std::exception &e) {
    return "[Error reading " + path + ": " + e.what() + "]";
  }
}

void Agent::toggle_shell_mode() {
  shell_mode_ = !shell_mode_;
  if (shell_mode_) {
    std::cout << "Entering shell mode. Type commands directly or '!' to exit."
              << std::endl;
  } else {
    std::cout << "Exiting shell mode." << std::endl;
  }
}

bool Agent::should_skip_file(const std::string &file_path,
                             const std::string &ext) {
  std::vector<std::string> skip_extensions = {
      ".exe", ".dll",  ".so",  ".dylib", ".a",   ".lib", ".obj", ".o",   ".png",
      ".jpg", ".jpeg", ".gif", ".bmp",   ".ico", ".svg", ".mp3", ".mp4", ".avi",
      ".mov", ".wav",  ".pdf", ".zip",   ".tar", ".gz",  ".7z",  ".rar"};

  for (const auto &skip_ext : skip_extensions) {
    if (ext == skip_ext)
      return true;
  }

  if (file_path.find("node_modules") != std::string::npos ||
      file_path.find(".git") != std::string::npos ||
      file_path.find("build") != std::string::npos ||
      file_path.find("dist") != std::string::npos ||
      file_path.find(".cache") != std::string::npos) {
    return true;
  }

  return false;
}

void Agent::show_meta_help() {
  std::cout << "Available meta commands:" << std::endl;
  std::cout << "  /help or /?           - Show this help" << std::endl;
  std::cout << "  /clear                - Clear screen" << std::endl;
  std::cout << "  /chat save <tag>      - Save conversation state" << std::endl;
  std::cout << "  /chat resume <tag>    - Resume conversation state"
            << std::endl;
  std::cout << "  /chat list            - List saved conversations"
            << std::endl;
  std::cout << "  /tools                - Show available tools" << std::endl;
  std::cout << "  /memory show          - Show memory context" << std::endl;
  std::cout << "  /memory add <text>    - Add to memory" << std::endl;
  std::cout << "  /compress             - Compress conversation context"
            << std::endl;
  std::cout << "  /stats                - Show session statistics" << std::endl;
  std::cout << "  /context show         - Show hierarchical context"
            << std::endl;
  std::cout << "  /context refresh      - Refresh context cache" << std::endl;
  std::cout << "  /context create       - Create AGENTWARE.md file"
            << std::endl;
  std::cout << "  /files <patterns>     - Read multiple files with patterns"
            << std::endl;
  std::cout << "  /fetch <url> [format] - Fetch web content (text/json/raw)"
            << std::endl;
  std::cout
      << "  /checkpoint <cmd>     - Manage checkpoints (create/list/delete)"
      << std::endl;
  std::cout << "  /restore [id]         - List or restore from checkpoint"
            << std::endl;
  std::cout << "  /mcp <cmd>            - MCP server management "
               "(servers/tools/resources)"
            << std::endl;
  std::cout << "  /theme <cmd>          - Theme management (list/set/preview)"
            << std::endl;
  std::cout
      << "  /auth <cmd>           - Authentication management (providers/keys)"
      << std::endl;
  std::cout << "  /sandbox <cmd>        - Sandboxed command execution"
            << std::endl;
  std::cout << "  /error <cmd>          - Error management and reporting"
            << std::endl;
  std::cout << "  /github repo:owner/repo - Get repository info" << std::endl;
  std::cout << "  /github issues:owner/repo - List repository issues"
            << std::endl;
  std::cout << "  /github health:owner/repo - Run health check" << std::endl;
  std::cout << "  /quit or /exit        - Exit the program" << std::endl;
  std::cout << std::endl;
  std::cout << "File injection commands:" << std::endl;
  std::cout << "  @<path>               - Include file/directory content"
            << std::endl;
  std::cout << "  Example: @src/main.cpp What does this code do?" << std::endl;
  std::cout << std::endl;
  std::cout << "Shell commands:" << std::endl;
  std::cout << "  !<command>            - Execute shell command" << std::endl;
  std::cout << "  !                     - Toggle shell mode" << std::endl;
}

void Agent::clear_screen() {
  // Clear screen using ANSI escape codes
  std::cout << "\033[2J\033[H" << std::flush;
}

void Agent::handle_chat_management(const std::string &command) {
  if (command.rfind("save ", 0) == 0) {
    std::string tag = trim_copy(command.substr(5));
    if (tag.empty()) {
      std::cout << "Usage: /chat save <tag>" << std::endl;
      return;
    }
    memory_->save_conversation_state(tag);
    std::cout << "Conversation saved as: " << tag << std::endl;
  } else if (command.rfind("resume ", 0) == 0) {
    std::string tag = trim_copy(command.substr(7));
    if (tag.empty()) {
      std::cout << "Usage: /chat resume <tag>" << std::endl;
      return;
    }
    if (memory_->resume_conversation_state(tag)) {
      std::cout << "Conversation resumed: " << tag << std::endl;
    } else {
      std::cout << "Could not resume conversation: " << tag << std::endl;
    }
  } else if (command == "list") {
    auto conversations = memory_->list_conversation_states();
    if (conversations.empty()) {
      std::cout << "No saved conversations." << std::endl;
    } else {
      std::cout << "Saved conversations:" << std::endl;
      for (const auto &conv : conversations) {
        std::cout << "  " << conv << std::endl;
      }
    }
  } else {
    std::cout << "Usage: /chat [save|resume|list] <tag>" << std::endl;
  }
}

void Agent::show_available_tools() {
  std::cout << "Available tools:" << std::endl;
  std::cout << "  File Operations:" << std::endl;
  std::cout << "    read:file[:start:count]     - Read file content"
            << std::endl;
  std::cout << "    write:file content          - Write to file" << std::endl;
  std::cout << "    replace:file:old:new        - Replace text in file"
            << std::endl;
  std::cout << "    grep:pattern[:dir[:ext]]    - Search in files" << std::endl;
  std::cout << "  Project Analysis:" << std::endl;
  std::cout << "    analyze:[path]              - Analyze project structure"
            << std::endl;
  std::cout << "    components:[path]           - Find main components"
            << std::endl;
  std::cout << "    todos:[path]                - Find task comments"
            << std::endl;
  std::cout << "    tree:[path]                 - Show directory tree"
            << std::endl;
  std::cout << "  Git Operations:" << std::endl;
  std::cout << "    git:status                  - Show git status" << std::endl;
  std::cout << "    git:log                     - Show git log" << std::endl;
  std::cout << "    git:analyze                 - Analyze repository"
            << std::endl;
  std::cout << "  System:" << std::endl;
  std::cout << "    cmd:command                 - Execute shell command"
            << std::endl;
  std::cout << "    search:query                - Web search" << std::endl;
  std::cout << "    remember:fact               - Save to memory" << std::endl;
  std::cout << "    memory                      - Show memories" << std::endl;
  std::cout << "  GitHub Operations:" << std::endl;
  std::cout << "    github:repo:owner/repo      - Get repository info"
            << std::endl;
  std::cout << "    github:issues:owner/repo    - List repository issues"
            << std::endl;
  std::cout << "    github:health:owner/repo    - Run health check"
            << std::endl;
}

void Agent::show_memory_context() {
  std::string context = memory_->get_context_string();
  if (context.empty()) {
    std::cout << "No memory context available." << std::endl;
  } else {
    std::cout << "Current memory context:" << std::endl;
    std::cout << context << std::endl;
  }
}

void Agent::add_to_memory(const std::string &text) {
  if (text.empty()) {
    std::cout << "Usage: /memory add <text>" << std::endl;
    return;
  }
  memory_->save_global_fact(text);
  std::cout << "Added to memory: " << text << std::endl;
}

void Agent::compress_context() {
  try {
    std::cout << "Analyzing conversation context for compression..."
              << std::endl;

    // Get compressible content from memory
    std::string compressible_content = memory_->get_compressible_context();

    if (compressible_content.empty()) {
      std::cout << "Not enough conversation history to compress (need at least "
                   "5 interactions)."
                << std::endl;
      return;
    }

    // Create compression prompt
    std::string compression_prompt =
        "Please create a concise summary of the following conversation "
        "history. "
        "Preserve key information, decisions made, important facts discovered, "
        "and context that would be needed for future interactions. "
        "Focus on actionable information and maintain continuity. Keep the "
        "summary under 500 words.\n\n"
        "Conversation History:\n" +
        compressible_content;

    std::cout << "Generating AI-powered summary..." << std::endl;

    // Use AI service to generate summary
    std::string summary = ai_service_->chat(compression_prompt, "");

    if (summary.empty()) {
      std::cout << "Failed to generate compression summary." << std::endl;
      return;
    }

    // Apply compression to memory
    memory_->compress_memory(summary);

    std::cout << " Context successfully compressed!" << std::endl;
    std::cout << "Original interactions have been summarized and recent "
                 "context preserved."
              << std::endl;
    std::cout << "Backup created automatically for safety." << std::endl;

  } catch (const std::exception &e) {
    std::cout << "Error during context compression: " << e.what() << std::endl;
  }
}

void Agent::show_session_stats() {
  std::string mode_str =
      (mode_ == Agent::Mode::MODE_TOGETHER   ? "Together AI"
       : mode_ == Agent::Mode::MODE_CEREBRAS ? "Cerebras"
                                             : "Local Ollama");
  std::cout << "Session Statistics:" << std::endl;
  std::cout << "  Mode: " << mode_str << std::endl;
  std::cout << "  Shell Mode: " << (shell_mode_ ? "Active" : "Inactive")
            << std::endl;
  std::cout << "  Commands Processed: " << command_count_ << std::endl;
  std::cout << "  Token Usage: " << token_usage_ << std::endl;
}

void Agent::handle_context_management(const std::string &command) {
  if (command == "show") {
    std::string context =
        Services::ContextService::load_hierarchical_context(".");
    if (context.empty()) {
      std::cout << "No hierarchical context found." << std::endl;
      std::cout << "Create a AGENTWARE.md file with /context create"
                << std::endl;
    } else {
      std::cout << "Hierarchical Context:" << std::endl;
      std::cout << context << std::endl;
    }
  } else if (command == "refresh") {
    Services::ContextService::refresh_context_cache();
    std::cout << "Context cache refreshed." << std::endl;
  } else if (command == "create") {
    if (Services::ContextService::create_context_file(".")) {
      std::cout << "Created AGENTWARE.md in current directory." << std::endl;
      std::cout << "Edit it to provide context for this project." << std::endl;
    } else {
      std::cout << "Could not create AGENTWARE.md (file may already exist)."
                << std::endl;
    }
  } else {
    std::cout << "Usage: /context [show|refresh|create]" << std::endl;
  }
}

void Agent::handle_multi_file_command(const std::string &command) {
  if (command.empty()) {
    std::cout << "Usage: /files <path1> [path2] [--include pattern] [--exclude "
                 "pattern]"
              << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  /files src/" << std::endl;
    std::cout << "  /files . --include *.cpp --include *.h" << std::endl;
    std::cout << "  /files src/ --exclude *.log --exclude build/*" << std::endl;
    return;
  }

  // Parse command arguments
  std::vector<std::string> paths;
  Services::MultiFileOptions options;

  std::istringstream iss(command);
  std::string token;
  std::string current_flag;

  while (iss >> token) {
    if (token == "--include") {
      current_flag = "include";
    } else if (token == "--exclude") {
      current_flag = "exclude";
    } else if (token == "--no-gitignore") {
      options.respect_gitignore = false;
      current_flag.clear();
    } else if (token == "--no-recursive") {
      options.recursive = false;
      current_flag.clear();
    } else {
      if (current_flag == "include") {
        options.include_patterns.push_back(token);
      } else if (current_flag == "exclude") {
        options.exclude_patterns.push_back(token);
      } else {
        paths.push_back(token);
      }
    }
  }

  if (paths.empty()) {
    paths.push_back("."); // Default to current directory
  }

  // Read files
  auto files = Services::MultiFileService::read_many_files(paths, options);

  if (files.empty()) {
    std::cout << "No files found matching the criteria." << std::endl;
    return;
  }

  // Format and display
  std::string formatted = Services::MultiFileService::format_multi_file_content(
      files, "Multi-file read result");

  std::cout << formatted << std::endl;

  // Save to memory for AI context
  memory_->save_interaction("/files " + command, formatted);
}

void Agent::handle_web_fetch_command(const std::string &command) {
  if (command.empty()) {
    std::cout << "Usage: /fetch <url> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  text    - Extract text content from HTML" << std::endl;
    std::cout << "  json    - Parse and format JSON response" << std::endl;
    std::cout << "  raw     - Return raw response content" << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  /fetch https://api.github.com/users/octocat json"
              << std::endl;
    std::cout << "  /fetch https://example.com text" << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string url, format = "text";
  iss >> url >> format;

  if (url.empty()) {
    std::cout << "Error: URL is required" << std::endl;
    return;
  }

  if (!Services::WebService::is_valid_url(url)) {
    std::cout << "Error: Invalid URL format. Use http:// or https://"
              << std::endl;
    return;
  }

  std::cout << "Fetching: " << url << std::endl;

  std::string result;
  if (format == "json") {
    result = Services::WebService::fetch_json(url);
  } else if (format == "raw") {
    auto response = Services::WebService::fetch_url(url);
    if (response.success) {
      result = "Status: " + std::to_string(response.status_code) + "\n";
      result += "Content-Type: " + response.content_type + "\n\n";
      result += response.content;
      if (result.length() > MAX_RESPONSE_LENGTH) {
        result = result.substr(0, MAX_RESPONSE_LENGTH) +
                 "\n\n[Content truncated - showing first " +
                 std::to_string(MAX_RESPONSE_LENGTH) + " characters]";
      }
    } else {
      result = "Error: " + response.error_message;
    }
  } else { // default to text
    result = Services::WebService::fetch_text(url);
    return;
  }

  // Process the fetched content with AI if needed
  if (result.length() > 1000) { // Only process large content with AI
    std::atomic<bool> done(false);
    std::thread spin([&done]() { Utils::UI::spinner(done); });

    std::string context = memory_->get_context_string();
    std::string ai_prompt = "Summarize the following content:\n\n" + result;
    std::string response = ai_service_->chat(ai_prompt, context);

    done = true;
    if (spin.joinable())
      spin.join();

    if (!response.empty()) {
      std::cout << "\nAI Summary:\n" << response << std::endl;
      memory_->save_interaction("web_fetch_summary", response);
    }
  }

  std::cout << result << std::endl;

  // Save to memory for AI context
  memory_->save_interaction("/fetch " + command, result);
}

void Agent::handle_checkpoint_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Checkpoint commands:" << std::endl;
    std::cout
        << "  /checkpoint create <name> [description] - Create new checkpoint"
        << std::endl;
    std::cout
        << "  /checkpoint list                       - List all checkpoints"
        << std::endl;
    std::cout
        << "  /checkpoint info <id>                  - Show checkpoint details"
        << std::endl;
    std::cout << "  /checkpoint delete <id>                - Delete checkpoint"
              << std::endl;
    std::cout << "  /checkpoint cleanup [count]            - Keep only N "
                 "recent checkpoints"
              << std::endl;
    std::cout << "  /restore                               - List checkpoints "
                 "for restore"
              << std::endl;
    std::cout
        << "  /restore <id>                          - Restore from checkpoint"
        << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    if (action == "create") {
      std::string name, description;
      iss >> name;
      std::getline(iss, description);
      if (!description.empty() && description[0] == ' ') {
        description = description.substr(1); // Remove leading space
      }

      if (name.empty()) {
        std::cout << "Error: Checkpoint name is required" << std::endl;
        std::cout << "Usage: /checkpoint create <name> [description]"
                  << std::endl;
        return;
      }

      std::cout << "Creating checkpoint '" << name << "'..." << std::endl;
      std::string checkpoint_id =
          Services::CheckpointService::create_checkpoint(name, description);
      std::cout << " Checkpoint created with ID: " << checkpoint_id
                << std::endl;

    } else if (action == "list") {
      auto checkpoints = Services::CheckpointService::list_checkpoints();
      if (checkpoints.empty()) {
        std::cout << "No checkpoints found." << std::endl;
        return;
      }

      std::cout << "Available checkpoints:" << std::endl;
      for (const auto &cp : checkpoints) {
        std::cout << "  " << cp.id << " - " << cp.name << std::endl;
        std::cout << "    Created: " << cp.timestamp << std::endl;
        if (!cp.description.empty()) {
          std::cout << "    Description: " << cp.description << std::endl;
        }
        std::cout << "    Files: " << cp.backed_up_files.size() << " ("
                  << (cp.total_size / 1024) << " KB)" << std::endl;
        std::cout << std::endl;
      }

    } else if (action == "info") {
      std::string checkpoint_id;
      iss >> checkpoint_id;
      if (checkpoint_id.empty()) {
        std::cout << "Error: Checkpoint ID is required" << std::endl;
        return;
      }

      auto info =
          Services::CheckpointService::get_checkpoint_info(checkpoint_id);
      std::cout << "Checkpoint Details:" << std::endl;
      std::cout << "  ID: " << info.id << std::endl;
      std::cout << "  Name: " << info.name << std::endl;
      std::cout << "  Created: " << info.timestamp << std::endl;
      if (!info.description.empty()) {
        std::cout << "  Description: " << info.description << std::endl;
      }
      std::cout << "  Total Size: " << (info.total_size / 1024) << " KB"
                << std::endl;
      std::cout << "  Files (" << info.backed_up_files.size()
                << "):" << std::endl;
      for (const auto &file : info.backed_up_files) {
        std::cout << "    " << file << std::endl;
      }

    } else if (action == "delete") {
      std::string checkpoint_id;
      iss >> checkpoint_id;
      if (checkpoint_id.empty()) {
        std::cout << "Error: Checkpoint ID is required" << std::endl;
        return;
      }

      if (Services::CheckpointService::delete_checkpoint(checkpoint_id)) {
        std::cout << " Checkpoint " << checkpoint_id << " deleted" << std::endl;
      } else {
        std::cout << "Error: Failed to delete checkpoint " << checkpoint_id
                  << std::endl;
      }

    } else if (action == "cleanup") {
      int keep_count = 10;
      iss >> keep_count;
      if (keep_count < 1)
        keep_count = 10;

      Services::CheckpointService::cleanup_old_checkpoints(keep_count);
      std::cout << " Cleaned up old checkpoints, keeping " << keep_count
                << " most recent" << std::endl;

    } else if (action == "restore") {
      std::string checkpoint_id;
      iss >> checkpoint_id;

      if (checkpoint_id.empty()) {
        // List checkpoints for selection
        auto checkpoints = Services::CheckpointService::list_checkpoints();
        if (checkpoints.empty()) {
          std::cout << "No checkpoints available for restore." << std::endl;
          return;
        }

        std::cout << "Available checkpoints for restore:" << std::endl;
        for (const auto &cp : checkpoints) {
          std::cout << "  " << cp.id << " - " << cp.name << " (" << cp.timestamp
                    << ")" << std::endl;
        }
        std::cout << std::endl << "Use: /restore <checkpoint_id>" << std::endl;
        return;
      }

      std::cout << "Restoring from checkpoint " << checkpoint_id << "..."
                << std::endl;
      std::cout
          << "Note: A backup will be created automatically before restore."
          << std::endl;

      Services::RestoreOptions options;
      options.create_backup_before_restore = true;
      options.restore_memory = true;
      options.restore_files = true;

      if (Services::CheckpointService::restore_checkpoint(checkpoint_id,
                                                          options)) {
        std::cout << " Successfully restored from checkpoint " << checkpoint_id
                  << std::endl;
        std::cout << "Note: You may need to restart the application to see all "
                     "changes."
                  << std::endl;
      } else {
        std::cout << "Error: Failed to restore from checkpoint "
                  << checkpoint_id << std::endl;
      }

    } else {
      std::cout << "Unknown checkpoint command: " << action << std::endl;
      std::cout << "Use '/checkpoint help' for available commands."
                << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  // Save to memory for AI context
  memory_->save_interaction("/checkpoint " + command,
                            "Checkpoint command executed");
}

void Agent::handle_mcp_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "MCP (Model Context Protocol) commands:" << std::endl;
    std::cout << "  /mcp servers                           - List registered "
                 "MCP servers"
              << std::endl;
    std::cout << "  /mcp status <server>                   - Show server status"
              << std::endl;
    std::cout << "  /mcp start <server>                    - Start MCP server"
              << std::endl;
    std::cout << "  /mcp stop <server>                     - Stop MCP server"
              << std::endl;
    std::cout
        << "  /mcp resources <server>                - List server resources"
        << std::endl;
    std::cout << "  /mcp read <server> <uri>               - Read resource "
                 "from server"
              << std::endl;
    std::cout << "  /mcp tools <server>                    - List server tools"
              << std::endl;
    std::cout << "  /mcp call <server> <tool> [args]       - Call server tool"
              << std::endl;
    std::cout
        << "  /mcp prompts <server>                  - List server prompts"
        << std::endl;
    std::cout << "  /mcp prompt <server> <name> [args]     - Get server prompt"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    // Initialize MCP service configuration
    Services::MCPService::load_server_config();

    if (action == "servers") {
      auto servers = Services::MCPService::list_mcp_servers();
      if (servers.empty()) {
        std::cout << "No MCP servers registered." << std::endl;
        std::cout
            << "Default servers (filesystem, git) will be added automatically."
            << std::endl;
        return;
      }

      std::cout << "Registered MCP servers:" << std::endl;
      for (const auto &server_name : servers) {
        auto server = Services::MCPService::get_mcp_server(server_name);
        std::string status =
            Services::MCPService::get_server_status(server_name);
        std::cout << "  " << server_name << " - " << status << std::endl;
        std::cout << "    Executable: " << server.executable;
        for (const auto &arg : server.args) {
          std::cout << " " << arg;
        }
        std::cout << std::endl;
      }

    } else if (action == "status") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      std::string status = Services::MCPService::get_server_status(server_name);
      std::cout << "Server '" << server_name << "' status: " << status
                << std::endl;

    } else if (action == "start") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      std::cout << "Starting MCP server '" << server_name << "'..."
                << std::endl;
      if (Services::MCPService::is_server_running(server_name)) {
        std::cout << "Server is already running." << std::endl;
      } else {
        // This would start the actual server in a full implementation
        std::cout << " MCP server '" << server_name << "' started (simulated)"
                  << std::endl;
        std::cout << "Note: Full MCP server integration requires process "
                     "management implementation."
                  << std::endl;
      }

    } else if (action == "stop") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      std::cout << "Stopping MCP server '" << server_name << "'..."
                << std::endl;
      std::cout << " MCP server '" << server_name << "' stopped (simulated)"
                << std::endl;

    } else if (action == "resources") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      auto resources = Services::MCPService::list_resources(server_name);
      if (resources.empty()) {
        std::cout << "No resources available from server '" << server_name
                  << "'" << std::endl;
        return;
      }

      std::cout << "Resources from server '" << server_name
                << "':" << std::endl;
      for (const auto &resource : resources) {
        std::cout << "  " << resource.uri << " - " << resource.name
                  << std::endl;
        if (!resource.description.empty()) {
          std::cout << "    Description: " << resource.description << std::endl;
        }
        if (!resource.mime_type.empty()) {
          std::cout << "    Type: " << resource.mime_type << std::endl;
        }
      }

    } else if (action == "read") {
      std::string server_name, uri;
      iss >> server_name >> uri;
      if (server_name.empty() || uri.empty()) {
        std::cout << "Error: Server name and URI are required" << std::endl;
        return;
      }

      std::cout << "Reading resource '" << uri << "' from server '"
                << server_name << "'..." << std::endl;
      std::string content =
          Services::MCPService::read_resource(server_name, uri);
      std::cout << content << std::endl;

    } else if (action == "tools") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      auto tools = Services::MCPService::list_tools(server_name);
      if (tools.empty()) {
        std::cout << "No tools available from server '" << server_name << "'"
                  << std::endl;
        return;
      }

      std::cout << "Tools from server '" << server_name << "':" << std::endl;
      for (const auto &tool : tools) {
        std::cout << "  " << tool.name << " - " << tool.description
                  << std::endl;
      }

    } else if (action == "call") {
      std::string server_name, tool_name, args_str;
      iss >> server_name >> tool_name;
      std::getline(iss, args_str);
      if (!args_str.empty() && args_str[0] == ' ') {
        args_str = args_str.substr(1); // Remove leading space
      }

      if (server_name.empty() || tool_name.empty()) {
        std::cout << "Error: Server name and tool name are required"
                  << std::endl;
        return;
      }

      nlohmann::json args = nlohmann::json::object();
      if (!args_str.empty()) {
        try {
          args = nlohmann::json::parse(args_str);
        } catch (const std::exception &) {
          // If not valid JSON, treat as simple string argument
          args["input"] = args_str;
        }
      }

      std::cout << "Calling tool '" << tool_name << "' on server '"
                << server_name << "'..." << std::endl;
      auto result =
          Services::MCPService::call_tool(server_name, tool_name, args);
      std::cout << result.dump(2) << std::endl;

    } else if (action == "prompts") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      auto prompts = Services::MCPService::list_prompts(server_name);
      if (prompts.empty()) {
        std::cout << "No prompts available from server '" << server_name << "'"
                  << std::endl;
        return;
      }

      std::cout << "Prompts from server '" << server_name << "':" << std::endl;
      for (const auto &prompt : prompts) {
        std::cout << "  " << prompt.name << " - " << prompt.description
                  << std::endl;
        if (!prompt.arguments.empty()) {
          std::cout << "    Arguments: ";
          for (size_t i = 0; i < prompt.arguments.size(); ++i) {
            if (i > 0)
              std::cout << ", ";
            std::cout << prompt.arguments[i];
          }
          std::cout << std::endl;
        }
      }

    } else if (action == "prompt") {
      std::string server_name, prompt_name, args_str;
      iss >> server_name >> prompt_name;
      std::getline(iss, args_str);
      if (!args_str.empty() && args_str[0] == ' ') {
        args_str = args_str.substr(1); // Remove leading space
      }

      if (server_name.empty() || prompt_name.empty()) {
        std::cout << "Error: Server name and prompt name are required"
                  << std::endl;
        return;
      }

      nlohmann::json args = nlohmann::json::object();
      if (!args_str.empty()) {
        try {
          args = nlohmann::json::parse(args_str);
        } catch (const std::exception &) {
          // If not valid JSON, treat as simple string argument
          args["input"] = args_str;
        }
      }

      std::cout << "Getting prompt '" << prompt_name << "' from server '"
                << server_name << "'..." << std::endl;
      std::string prompt_text =
          Services::MCPService::get_prompt(server_name, prompt_name, args);
      std::cout << prompt_text << std::endl;

    } else {
      std::cout << "Unknown MCP command: " << action << std::endl;
      std::cout << "Use '/mcp help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  // Save to memory for AI context
  memory_->save_interaction("/mcp " + command, "MCP command executed");
}

void Agent::handle_theme_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Theme commands:" << std::endl;
    std::cout
        << "  /theme list                            - List available themes"
        << std::endl;
    std::cout << "  /theme set <name>                      - Set active theme"
              << std::endl;
    std::cout
        << "  /theme preview <name>                  - Preview theme colors"
        << std::endl;
    std::cout << "  /theme current                         - Show current theme"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::ThemeService::initialize();

    if (action == "list") {
      auto themes = Services::ThemeService::list_available_themes();
      std::cout << "Available themes:" << std::endl;
      for (const auto &theme_name : themes) {
        auto theme_info = Services::ThemeService::get_theme_info(theme_name);
        std::string current =
            (theme_name == Services::ThemeService::get_current_theme())
                ? " (current)"
                : "";
        std::cout << "  " << Services::ThemeService::colorize_accent(theme_name)
                  << current << std::endl;
        std::cout << "    " << theme_info.description << std::endl;
      }

    } else if (action == "set") {
      std::string theme_name;
      iss >> theme_name;
      if (theme_name.empty()) {
        std::cout << "Error: Theme name is required" << std::endl;
        return;
      }

      if (Services::ThemeService::set_theme(theme_name)) {
        std::cout << Services::ThemeService::colorize_success(
                         " Theme set to: " + theme_name)
                  << std::endl;
      } else {
        std::cout << Services::ThemeService::colorize_error(
                         "Error: Theme not found: " + theme_name)
                  << std::endl;
      }

    } else if (action == "preview") {
      std::string theme_name;
      iss >> theme_name;
      if (theme_name.empty()) {
        std::cout << "Error: Theme name is required" << std::endl;
        return;
      }

      Services::ThemeService::print_theme_preview(theme_name);

    } else if (action == "current") {
      std::string current_theme = Services::ThemeService::get_current_theme();
      auto theme_info = Services::ThemeService::get_theme_info(current_theme);
      std::cout << "Current theme: "
                << Services::ThemeService::colorize_accent(current_theme)
                << std::endl;
      std::cout << "Description: " << theme_info.description << std::endl;

    } else {
      std::cout << "Unknown theme command: " << action << std::endl;
      std::cout << "Use '/theme help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  memory_->save_interaction("/theme " + command, "Theme command executed");
}

void Agent::handle_auth_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Authentication commands:" << std::endl;
    std::cout
        << "  /auth providers                        - List available providers"
        << std::endl;
    std::cout
        << "  /auth set <provider>                   - Set active provider"
        << std::endl;
    std::cout
        << "  /auth key <provider> <key>             - Set API key for provider"
        << std::endl;
    std::cout
        << "  /auth status [provider]                - Show provider status"
        << std::endl;
    std::cout << "  /auth test <provider>                   - Test provider "
                 "connection"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::AuthService::initialize();

    if (action == "providers") {
      auto providers = Services::AuthService::list_providers();
      std::cout << "Available authentication providers:" << std::endl;
      for (const auto &provider_name : providers) {
        auto provider_info =
            Services::AuthService::get_provider_info(provider_name);
        std::string status =
            Services::AuthService::get_provider_status(provider_name);
        std::string active = provider_info.is_active ? " (active)" : "";
        std::cout << "  " << provider_info.display_name << active << " - "
                  << status << std::endl;
        std::cout << "    Model: " << provider_info.model << std::endl;
      }

    } else if (action == "set") {
      std::string provider_name;
      iss >> provider_name;
      if (provider_name.empty()) {
        std::cout << "Error: Provider name is required" << std::endl;
        return;
      }

      if (Services::AuthService::set_active_provider(provider_name)) {
        std::cout << " Active provider set to: " << provider_name << std::endl;
      } else {
        std::cout << "Error: Provider not found: " << provider_name
                  << std::endl;
      }

    } else if (action == "key") {
      std::string provider_name, api_key;
      iss >> provider_name >> api_key;
      if (provider_name.empty() || api_key.empty()) {
        std::cout << "Error: Provider name and API key are required"
                  << std::endl;
        return;
      }

      if (Services::AuthService::set_api_key(provider_name, api_key)) {
        std::cout << " API key set for provider: " << provider_name
                  << std::endl;
      } else {
        std::cout << "Error: Failed to set API key for provider: "
                  << provider_name << std::endl;
      }

    } else if (action == "status") {
      std::string provider_name;
      iss >> provider_name;
      if (provider_name.empty()) {
        provider_name = Services::AuthService::get_active_provider();
      }

      std::string status =
          Services::AuthService::get_provider_status(provider_name);
      auto provider_info =
          Services::AuthService::get_provider_info(provider_name);
      std::cout << "Provider: " << provider_info.display_name << std::endl;
      std::cout << "Status: " << status << std::endl;
      std::cout << "Model: " << provider_info.model << std::endl;
      std::cout << "Base URL: " << provider_info.base_url << std::endl;

    } else if (action == "test") {
      std::string provider_name;
      iss >> provider_name;
      if (provider_name.empty()) {
        std::cout << "Error: Provider name is required" << std::endl;
        return;
      }

      if (Services::AuthService::test_provider_connection(provider_name)) {
        std::cout << " Connection test successful for: " << provider_name
                  << std::endl;
      } else {
        std::cout << " Connection test failed for: " << provider_name
                  << std::endl;
      }

    } else {
      std::cout << "Unknown auth command: " << action << std::endl;
      std::cout << "Use '/auth help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  memory_->save_interaction("/auth " + command, "Auth command executed");
}

void Agent::handle_sandbox_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Sandbox commands:" << std::endl;
    std::cout << "  /sandbox run <command>                 - Execute command "
                 "in sandbox"
              << std::endl;
    std::cout << "  /sandbox list                          - List available "
                 "sandbox configs"
              << std::endl;
    std::cout << "  /sandbox status                        - Show Docker status"
              << std::endl;
    std::cout
        << "  /sandbox containers                    - List active containers"
        << std::endl;
    std::cout
        << "  /sandbox cleanup                       - Clean up old containers"
        << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::SandboxService::initialize();

    if (action == "run") {
      std::string cmd;
      std::getline(iss, cmd);
      if (!cmd.empty() && cmd[0] == ' ') {
        cmd = cmd.substr(1);
      }
      if (cmd.empty()) {
        std::cout << "Error: Command is required" << std::endl;
        return;
      }

      std::cout << "Executing in sandbox: " << cmd << std::endl;
      auto result = Services::SandboxService::execute_command(cmd);

      std::cout << "Exit code: " << result.exit_code << std::endl;
      std::cout << "Execution time: " << result.execution_time_seconds << "s"
                << std::endl;
      if (!result.stdout_output.empty()) {
        std::cout << "Output:\n" << result.stdout_output << std::endl;
      }
      if (!result.error_message.empty()) {
        std::cout << "Error: " << result.error_message << std::endl;
      }

    } else if (action == "list") {
      auto configs = Services::SandboxService::list_sandbox_configs();
      std::cout << "Available sandbox configurations:" << std::endl;
      for (const auto &config_name : configs) {
        auto config = Services::SandboxService::get_sandbox_config(config_name);
        std::cout << "  " << config_name << " - " << config.image << std::endl;
        std::cout << "    Memory: " << config.memory_limit_mb
                  << "MB, CPU: " << config.cpu_limit_percent << "%"
                  << std::endl;
      }

    } else if (action == "status") {
      if (Services::SandboxService::check_docker_installation()) {
        std::cout << "Docker Status: Available" << std::endl;
        std::cout << "Version: "
                  << Services::SandboxService::get_docker_version()
                  << std::endl;
      } else {
        std::cout << "Docker Status: Not Available" << std::endl;
        std::cout << "Please install Docker to use sandbox features."
                  << std::endl;
      }

    } else if (action == "containers") {
      auto containers = Services::SandboxService::list_active_containers();
      if (containers.empty()) {
        std::cout << "No active sandbox containers." << std::endl;
      } else {
        std::cout << "Active sandbox containers:" << std::endl;
        for (const auto &container : containers) {
          std::cout << "  " << container << std::endl;
        }
      }

    } else if (action == "cleanup") {
      Services::SandboxService::cleanup_old_containers();
      std::cout << " Cleaned up old sandbox containers" << std::endl;

    } else {
      std::cout << "Unknown sandbox command: " << action << std::endl;
      std::cout << "Use '/sandbox help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  memory_->save_interaction("/sandbox " + command, "Sandbox command executed");
}

void Agent::handle_error_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Error management commands:" << std::endl;
    std::cout << "  /error report                          - Show error summary"
              << std::endl;
    std::cout << "  /error recent [count]                  - Show recent errors"
              << std::endl;
    std::cout << "  /error clear                           - Clear error log"
              << std::endl;
    std::cout << "  /error export <path>                   - Export error log"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::ErrorService::initialize();

    if (action == "report") {
      Services::ErrorService::print_error_report();

    } else if (action == "recent") {
      int count = 5;
      iss >> count;
      if (count < 1)
        count = 5;

      auto recent_errors = Services::ErrorService::get_recent_errors(count);
      if (recent_errors.empty()) {
        std::cout << "No recent errors." << std::endl;
      } else {
        std::cout << "Recent errors (" << recent_errors.size()
                  << "):" << std::endl;
        for (const auto &error : recent_errors) {
          std::cout << Services::ErrorService::format_error(error, true)
                    << std::endl
                    << std::endl;
        }
      }

    } else if (action == "clear") {
      Services::ErrorService::clear_error_log();
      std::cout << " Error log cleared" << std::endl;

    } else if (action == "export") {
      std::string export_path;
      iss >> export_path;
      if (export_path.empty()) {
        export_path = "error_log_export.json";
      }

      if (Services::ErrorService::export_error_log(export_path)) {
        std::cout << " Error log exported to: " << export_path << std::endl;
      } else {
        std::cout << "Error: Failed to export error log" << std::endl;
      }

    } else {
      std::cout << "Unknown error command: " << action << std::endl;
      std::cout << "Use '/error help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  memory_->save_interaction("/error " + command, "Error command executed");
}
} // namespace Core
