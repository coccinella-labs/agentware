#pragma once
#include <functional>
#include <string>
#include <vector>

namespace Utils {
struct ValidationResult {
  bool is_valid;
  std::string error_message;
  std::vector<std::string> warnings;
};

class Validator {
public:
  // File path validation
  static ValidationResult
  validate_file_path(const std::string &path,
                     const std::string &base_directory = "");
  static ValidationResult validate_file_exists(const std::string &path);
  static ValidationResult validate_file_writable(const std::string &path);

  // Text validation
  static ValidationResult
  validate_non_empty(const std::string &text,
                     const std::string &field_name = "text");
  static ValidationResult validate_regex_pattern(const std::string &pattern);

  // Command validation
  static ValidationResult validate_command_safe(const std::string &command);
  static ValidationResult validate_search_query(const std::string &query);

  // Parameter validation
  static ValidationResult validate_line_range(int start_line, int line_count,
                                              int total_lines);
  static ValidationResult validate_replacement_count(int expected, int actual);

  // Composite validation
  static ValidationResult
  combine_results(const std::vector<ValidationResult> &results);

  // Utility functions
  static bool is_safe_path(const std::string &path);
  static bool is_text_file_extension(const std::string &extension);
  static std::string sanitize_input(const std::string &input);
};

// Validation helper macros
#define VALIDATE_AND_RETURN(validation)                                        \
  do {                                                                         \
    auto result = validation;                                                  \
    if (!result.is_valid) {                                                    \
      return result.error_message;                                             \
    }                                                                          \
  } while (0)

#define VALIDATE_PARAM(condition, message)                                     \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return ValidationResult{false, message, {}};                             \
    }                                                                          \
  } while (0)
} // namespace Utils
