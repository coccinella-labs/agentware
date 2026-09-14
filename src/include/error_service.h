#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Services {

enum class ErrorLevel : std::uint8_t { DEBUG, INFO, WARNING, ERROR, CRITICAL };

enum class ErrorCategory : std::uint8_t {
  VALIDATION,
  NETWORK,
  FILE_SYSTEM,
  AUTHENTICATION,
  COMMAND_EXECUTION,
  MEMORY_MANAGEMENT,
  CONFIGURATION,
  UNKNOWN
};

struct ErrorInfo {
  std::string id{};
  ErrorLevel level{ErrorLevel::DEBUG};
  ErrorCategory category{ErrorCategory::VALIDATION};
  std::string message{};
  std::string details{};
  std::string context{};
  std::chrono::system_clock::time_point timestamp{};
  std::vector<std::string> suggestions{};
  std::map<std::string, std::string> metadata{};
};

struct ValidationRule {
  std::string name{};
  std::function<bool(const std::string &)> validator{};
  std::string error_message{};
  std::vector<std::string> suggestions{};
};

class ErrorService {
private:
  static std::vector<ErrorInfo> error_log;
  static std::map<std::string, ValidationRule> validation_rules;
  static std::string get_error_log_path();
  static void ensure_error_directory();
  static std::string generate_error_id();
  static void initialize_validation_rules();

public:
  // Error logging and management
  static void initialize();
  static std::string
  log_error(ErrorLevel level, ErrorCategory category,
            const std::string &message, const std::string &context = "",
            const std::vector<std::string> &suggestions = {});
  static std::vector<ErrorInfo> get_recent_errors(size_t count = 10);
  static std::vector<ErrorInfo> get_errors_by_level(ErrorLevel level);
  static std::vector<ErrorInfo> get_errors_by_category(ErrorCategory category);
  static void clear_error_log();

  // Validation framework
  static bool validate_input(const std::string &rule_name,
                             const std::string &input,
                             std::string &error_message,
                             std::vector<std::string> &suggestions);
  static bool validate_file_path(const std::string &path,
                                 std::string &error_message);
  static bool validate_url(const std::string &url, std::string &error_message);
  static bool validate_command(const std::string &command,
                               std::string &error_message);
  static bool validate_api_key(const std::string &api_key,
                               std::string &error_message);
  static bool validate_json(const std::string &json_str,
                            std::string &error_message);

  // Error recovery
  static std::vector<std::string>
  suggest_recovery_actions(const std::string &error_id);
  static bool attempt_auto_recovery(const std::string &error_id);
  static void
  register_recovery_handler(ErrorCategory category,
                            std::function<bool(const ErrorInfo &)> handler);

  // Error reporting and formatting
  static std::string format_error(const ErrorInfo &error,
                                  bool include_suggestions = true);
  static std::string format_error_summary();
  static void print_error_report();
  static bool export_error_log(const std::string &export_path);

  // Utility functions
  static std::string error_level_to_string(ErrorLevel level);
  static std::string error_category_to_string(ErrorCategory category);
  static ErrorLevel string_to_error_level(const std::string &level_str);
  static ErrorCategory
  string_to_error_category(const std::string &category_str);

  // Configuration
  static void set_max_log_entries(size_t max_entries);
  static void set_log_level(ErrorLevel min_level);
  static bool save_error_config();
  static bool load_error_config();
};
} // namespace Services
