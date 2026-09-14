#include "error_service.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

namespace Services {

std::vector<ErrorInfo> ErrorService::error_log;
std::map<std::string, ValidationRule> ErrorService::validation_rules;

std::string ErrorService::get_error_log_path() { return "data/error_log.json"; }

void ErrorService::ensure_error_directory() {
  std::filesystem::create_directories("data");
}

std::string ErrorService::generate_error_id() {
  static int counter = 1;
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  ss << "ERR_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << "_"
     << std::setfill('0') << std::setw(3) << counter++;

  return ss.str();
}

void ErrorService::initialize_validation_rules() {
  // File path validation
  validation_rules["file_path"] = {
      "file_path",
      [](const std::string &path) {
        return !path.empty() && path.find("..") == std::string::npos &&
               path.length() < 4096;
      },
      "Invalid file path",
      {"Ensure path is not empty", "Avoid '..' in paths",
       "Keep path under 4096 characters"}};

  // URL validation
  validation_rules["url"] = {
      "url",
      [](const std::string &url) {
        std::regex url_regex(R"(^https?://[^\s/$.?#].[^\s]*$)");
        return std::regex_match(url, url_regex);
      },
      "Invalid URL format",
      {"Use http:// or https:// protocol", "Ensure no spaces in URL",
       "Check domain format"}};

  // Command validation
  validation_rules["command"] = {
      "command",
      [](const std::string &command) {
        // Basic command safety checks
        std::vector<std::string> dangerous = {"rm -rf", "format", "del /f",
                                              "sudo rm"};
        for (const auto &danger : dangerous) {
          if (command.find(danger) != std::string::npos) {
            return false;
          }
        }
        return !command.empty() && command.length() < 1024;
      },
      "Potentially dangerous or invalid command",
      {"Avoid destructive commands", "Keep commands under 1024 characters",
       "Use safe alternatives"}};

  // API key validation
  validation_rules["api_key"] = {
      "api_key",
      [](const std::string &key) {
        return !key.empty() && key.length() >= 10 && key.length() <= 512 &&
               key.find(' ') == std::string::npos;
      },
      "Invalid API key format",
      {"API key should be 10-512 characters", "No spaces allowed",
       "Check with provider documentation"}};

  // JSON validation
  validation_rules["json"] = {
      "json",
      [](const std::string &json_str) {
        try {
          auto parsed = nlohmann::json::parse(json_str);
          (void)parsed; // Suppress unused variable warning
          return true;
        } catch (...) {
          return false;
        }
      },
      "Invalid JSON format",
      {"Check bracket/brace matching", "Ensure proper quoting",
       "Validate syntax"}};
}

void ErrorService::initialize() {
  initialize_validation_rules();
  load_error_config();
}

std::string
ErrorService::log_error(ErrorLevel level, ErrorCategory category,
                        const std::string &message, const std::string &context,
                        const std::vector<std::string> &suggestions) {
  ErrorInfo error;
  error.id = generate_error_id();
  error.level = level;
  error.category = category;
  error.message = message;
  error.context = context;
  error.timestamp = std::chrono::system_clock::now();
  error.suggestions = suggestions;

  error_log.push_back(error);

  // Keep only recent errors (max 1000)
  if (error_log.size() > 1000) {
    error_log.erase(error_log.begin(), error_log.begin() + 100);
  }

  // Print error to console based on level
  if (level >= ErrorLevel::WARNING) {
    std::cerr << format_error(error, true) << std::endl;
  }

  return error.id;
}

std::vector<ErrorInfo> ErrorService::get_recent_errors(size_t count) {
  std::vector<ErrorInfo> recent;
  size_t start = error_log.size() > count ? error_log.size() - count : 0;

  for (size_t i = start; i < error_log.size(); ++i) {
    recent.push_back(error_log[i]);
  }

  return recent;
}

std::vector<ErrorInfo> ErrorService::get_errors_by_level(ErrorLevel level) {
  std::vector<ErrorInfo> filtered;
  for (const auto &error : error_log) {
    if (error.level == level) {
      filtered.push_back(error);
    }
  }
  return filtered;
}

std::vector<ErrorInfo>
ErrorService::get_errors_by_category(ErrorCategory category) {
  std::vector<ErrorInfo> filtered;
  for (const auto &error : error_log) {
    if (error.category == category) {
      filtered.push_back(error);
    }
  }
  return filtered;
}

void ErrorService::clear_error_log() { error_log.clear(); }

bool ErrorService::validate_input(const std::string &rule_name,
                                  const std::string &input,
                                  std::string &error_message,
                                  std::vector<std::string> &suggestions) {
  auto it = validation_rules.find(rule_name);
  if (it == validation_rules.end()) {
    error_message = "Unknown validation rule: " + rule_name;
    return false;
  }

  const ValidationRule &rule = it->second;
  if (!rule.validator(input)) {
    error_message = rule.error_message;
    suggestions = rule.suggestions;
    return false;
  }

  return true;
}

bool ErrorService::validate_file_path(const std::string &path,
                                      std::string &error_message) {
  std::vector<std::string> suggestions;
  return validate_input("file_path", path, error_message, suggestions);
}

bool ErrorService::validate_url(const std::string &url,
                                std::string &error_message) {
  std::vector<std::string> suggestions;
  return validate_input("url", url, error_message, suggestions);
}

bool ErrorService::validate_command(const std::string &command,
                                    std::string &error_message) {
  std::vector<std::string> suggestions;
  return validate_input("command", command, error_message, suggestions);
}

bool ErrorService::validate_api_key(const std::string &api_key,
                                    std::string &error_message) {
  std::vector<std::string> suggestions;
  return validate_input("api_key", api_key, error_message, suggestions);
}

bool ErrorService::validate_json(const std::string &json_str,
                                 std::string &error_message) {
  std::vector<std::string> suggestions;
  return validate_input("json", json_str, error_message, suggestions);
}

std::vector<std::string>
ErrorService::suggest_recovery_actions(const std::string &error_id) {
  for (const auto &error : error_log) {
    if (error.id == error_id) {
      return error.suggestions;
    }
  }
  return {};
}

bool ErrorService::attempt_auto_recovery(const std::string &error_id) {
  // Basic auto-recovery logic
  for (const auto &error : error_log) {
    if (error.id == error_id) {
      switch (error.category) {
      case ErrorCategory::FILE_SYSTEM:
        // Try to create missing directories
        return true;
      case ErrorCategory::NETWORK:
        // Retry network operations
        return false; // Not implemented
      default:
        return false;
      }
    }
  }
  return false;
}

void ErrorService::register_recovery_handler(
    [[maybe_unused]] ErrorCategory category,
    [[maybe_unused]] std::function<bool(const ErrorInfo &)> handler) {
  // Recovery handler registration - placeholder for future implementation
}

std::string ErrorService::format_error(const ErrorInfo &error,
                                       bool include_suggestions) {
  std::stringstream ss;

  auto time_t = std::chrono::system_clock::to_time_t(error.timestamp);
  ss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
     << "] ";
  ss << "[" << error_level_to_string(error.level) << "] ";
  ss << "[" << error_category_to_string(error.category) << "] ";
  ss << error.message;

  if (!error.context.empty()) {
    ss << " (Context: " << error.context << ")";
  }

  if (include_suggestions && !error.suggestions.empty()) {
    ss << "\nSuggestions:";
    for (const auto &suggestion : error.suggestions) {
      ss << "\n  - " << suggestion;
    }
  }

  return ss.str();
}

std::string ErrorService::format_error_summary() {
  std::map<ErrorLevel, int> level_counts;
  std::map<ErrorCategory, int> category_counts;

  for (const auto &error : error_log) {
    level_counts[error.level]++;
    category_counts[error.category]++;
  }

  std::stringstream ss;
  ss << "Error Summary (Total: " << error_log.size() << " errors)\n";

  ss << "By Level:\n";
  for (const auto &[level, count] : level_counts) {
    ss << "  " << error_level_to_string(level) << ": " << count << "\n";
  }

  ss << "By Category:\n";
  for (const auto &[category, count] : category_counts) {
    ss << "  " << error_category_to_string(category) << ": " << count << "\n";
  }

  return ss.str();
}

void ErrorService::print_error_report() {
  std::cout << format_error_summary() << std::endl;

  auto recent = get_recent_errors(5);
  if (!recent.empty()) {
    std::cout << "\nRecent Errors:" << std::endl;
    for (const auto &error : recent) {
      std::cout << format_error(error, true) << std::endl << std::endl;
    }
  }
}

bool ErrorService::export_error_log(const std::string &export_path) {
  try {
    nlohmann::json log_json = nlohmann::json::array();

    for (const auto &error : error_log) {
      nlohmann::json error_json;
      error_json["id"] = error.id;
      error_json["level"] = error_level_to_string(error.level);
      error_json["category"] = error_category_to_string(error.category);
      error_json["message"] = error.message;
      error_json["context"] = error.context;
      error_json["suggestions"] = error.suggestions;

      auto time_t = std::chrono::system_clock::to_time_t(error.timestamp);
      std::stringstream time_ss;
      time_ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
      error_json["timestamp"] = time_ss.str();

      log_json.push_back(error_json);
    }

    std::ofstream file(export_path);
    file << log_json.dump(2);

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to export error log: " << e.what() << std::endl;
    return false;
  }
}

std::string ErrorService::error_level_to_string(ErrorLevel level) {
  switch (level) {
  case ErrorLevel::DEBUG:
    return "DEBUG";
  case ErrorLevel::INFO:
    return "INFO";
  case ErrorLevel::WARNING:
    return "WARNING";
  case ErrorLevel::ERROR:
    return "ERROR";
  case ErrorLevel::CRITICAL:
    return "CRITICAL";
  default:
    return "UNKNOWN";
  }
}

std::string ErrorService::error_category_to_string(ErrorCategory category) {
  switch (category) {
  case ErrorCategory::VALIDATION:
    return "VALIDATION";
  case ErrorCategory::NETWORK:
    return "NETWORK";
  case ErrorCategory::FILE_SYSTEM:
    return "FILE_SYSTEM";
  case ErrorCategory::AUTHENTICATION:
    return "AUTHENTICATION";
  case ErrorCategory::COMMAND_EXECUTION:
    return "COMMAND_EXECUTION";
  case ErrorCategory::MEMORY_MANAGEMENT:
    return "MEMORY_MANAGEMENT";
  case ErrorCategory::CONFIGURATION:
    return "CONFIGURATION";
  case ErrorCategory::UNKNOWN:
    return "UNKNOWN";
  default:
    return "UNKNOWN";
  }
}

ErrorLevel ErrorService::string_to_error_level(const std::string &level_str) {
  if (level_str == "DEBUG")
    return ErrorLevel::DEBUG;
  if (level_str == "INFO")
    return ErrorLevel::INFO;
  if (level_str == "WARNING")
    return ErrorLevel::WARNING;
  if (level_str == "ERROR")
    return ErrorLevel::ERROR;
  if (level_str == "CRITICAL")
    return ErrorLevel::CRITICAL;
  return ErrorLevel::ERROR;
}

ErrorCategory
ErrorService::string_to_error_category(const std::string &category_str) {
  if (category_str == "VALIDATION")
    return ErrorCategory::VALIDATION;
  if (category_str == "NETWORK")
    return ErrorCategory::NETWORK;
  if (category_str == "FILE_SYSTEM")
    return ErrorCategory::FILE_SYSTEM;
  if (category_str == "AUTHENTICATION")
    return ErrorCategory::AUTHENTICATION;
  if (category_str == "COMMAND_EXECUTION")
    return ErrorCategory::COMMAND_EXECUTION;
  if (category_str == "MEMORY_MANAGEMENT")
    return ErrorCategory::MEMORY_MANAGEMENT;
  if (category_str == "CONFIGURATION")
    return ErrorCategory::CONFIGURATION;
  return ErrorCategory::UNKNOWN;
}

void ErrorService::set_max_log_entries([[maybe_unused]] size_t max_entries) {
  // Implementation for setting max log entries
}

void ErrorService::set_log_level([[maybe_unused]] ErrorLevel min_level) {
  // Implementation for setting minimum log level
}

bool ErrorService::save_error_config() {
  // Save error configuration - placeholder
  return true;
}

bool ErrorService::load_error_config() {
  // Load error configuration - placeholder
  return true;
}
} // namespace Services
