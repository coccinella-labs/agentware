#include "validation.h"
#include "file_service.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace Utils {

ValidationResult
Validator::validate_file_path(const std::string &path,
                              const std::string &base_directory) {
  ValidationResult result = {true, "", {}};

  if (path.empty()) {
    result.is_valid = false;
    result.error_message = "File path cannot be empty";
    return result;
  }

  // Check for dangerous path patterns
  if (path.find("..") != std::string::npos) {
    result.warnings.push_back("Path contains '..' which may be unsafe");
  }

  // Validate against base directory if provided
  if (!base_directory.empty()) {
    if (!Services::FileService::is_within_directory(path, base_directory)) {
      result.is_valid = false;
      result.error_message = "Path is outside the allowed base directory";
      return result;
    }
  }

  // Check path length
  if (path.length() > 260) { // Windows MAX_PATH limit
    result.warnings.push_back(
        "Path is very long and may cause issues on some systems");
  }

  return result;
}

ValidationResult Validator::validate_file_exists(const std::string &path) {
  ValidationResult result = {true, "", {}};

  if (!Services::FileService::file_exists(path)) {
    result.is_valid = false;
    result.error_message = "File does not exist: " + path;
  }

  return result;
}

ValidationResult Validator::validate_file_writable(const std::string &path) {
  ValidationResult result = {true, "", {}};

  try {
    std::filesystem::path file_path(path);
    std::filesystem::path parent_path = file_path.parent_path();

    // Check if parent directory exists and is writable
    if (!parent_path.empty() && !std::filesystem::exists(parent_path)) {
      // Try to create parent directories
      try {
        std::filesystem::create_directories(parent_path);
      } catch (const std::exception &e) {
        result.is_valid = false;
        result.error_message =
            "Cannot create parent directory: " + std::string(e.what());
        return result;
      }
    }

    // Check if file exists and is writable
    if (std::filesystem::exists(path)) {
      auto perms = std::filesystem::status(path).permissions();
      if ((perms & std::filesystem::perms::owner_write) ==
          std::filesystem::perms::none) {
        result.warnings.push_back("File may not be writable");
      }
    }

  } catch (const std::exception &e) {
    result.is_valid = false;
    result.error_message =
        "Error checking file writability: " + std::string(e.what());
  }

  return result;
}

ValidationResult Validator::validate_non_empty(const std::string &text,
                                               const std::string &field_name) {
  ValidationResult result = {true, "", {}};

  if (text.empty()) {
    result.is_valid = false;
    result.error_message = field_name + " cannot be empty";
  } else if (text.find_first_not_of(" \t\n\r") == std::string::npos) {
    result.is_valid = false;
    result.error_message = field_name + " cannot be only whitespace";
  }

  return result;
}

ValidationResult Validator::validate_regex_pattern(const std::string &pattern) {
  ValidationResult result = {true, "", {}};

  try {
    std::regex test_regex(pattern);
    (void)test_regex; // Suppress unused variable warning - we only care about
                      // construction validity
  } catch (const std::regex_error &e) {
    result.is_valid = false;
    result.error_message =
        "Invalid regular expression: " + std::string(e.what());
  }

  return result;
}

ValidationResult Validator::validate_command_safe(const std::string &command) {
  ValidationResult result = {true, "", {}};

  // List of potentially dangerous commands
  static const std::vector<std::string> dangerous_commands = {
      "rm",     "del",  "format",   "fdisk", "mkfs", "dd",      "shutdown",
      "reboot", "halt", "poweroff", "init",  "kill", "killall", "pkill"};

  std::string lower_command = command;
  std::transform(lower_command.begin(), lower_command.end(),
                 lower_command.begin(), ::tolower);

  for (const auto &dangerous : dangerous_commands) {
    if (lower_command.find(dangerous) != std::string::npos) {
      result.warnings.push_back(
          "Command contains potentially dangerous operation: " + dangerous);
      break;
    }
  }

  // Check for shell injection patterns
  if (command.find(";") != std::string::npos ||
      command.find("&&") != std::string::npos ||
      command.find("||") != std::string::npos ||
      command.find("|") != std::string::npos) {
    result.warnings.push_back(
        "Command contains shell operators that may be unsafe");
  }

  return result;
}

ValidationResult Validator::validate_search_query(const std::string &query) {
  ValidationResult result = validate_non_empty(query, "Search query");

  if (result.is_valid) {
    if (query.length() > 1000) {
      result.warnings.push_back("Search query is very long");
    }

    // Check for potentially problematic characters
    if (query.find_first_of("<>\"'&") != std::string::npos) {
      result.warnings.push_back(
          "Query contains special characters that may affect search results");
    }
  }

  return result;
}

ValidationResult Validator::validate_line_range(int start_line, int line_count,
                                                int total_lines) {
  ValidationResult result = {true, "", {}};

  if (start_line < 0) {
    result.is_valid = false;
    result.error_message = "Start line cannot be negative";
    return result;
  }

  if (line_count < -1) {
    result.is_valid = false;
    result.error_message = "Line count must be -1 (all) or positive";
    return result;
  }

  if (total_lines >= 0) {
    if (start_line >= total_lines) {
      result.is_valid = false;
      result.error_message = "Start line is beyond end of file";
      return result;
    }

    if (line_count > 0 && start_line + line_count > total_lines) {
      result.warnings.push_back("Requested range extends beyond end of file");
    }
  }

  return result;
}

ValidationResult Validator::validate_replacement_count(int expected,
                                                       int actual) {
  ValidationResult result = {true, "", {}};

  if (expected > 0 && expected != actual) {
    result.is_valid = false;
    result.error_message = "Expected " + std::to_string(expected) +
                           " replacements but found " + std::to_string(actual);
  } else if (actual == 0) {
    result.is_valid = false;
    result.error_message = "No matches found for replacement";
  }

  return result;
}

ValidationResult
Validator::combine_results(const std::vector<ValidationResult> &results) {
  ValidationResult combined = {true, "", {}};

  for (const auto &result : results) {
    if (!result.is_valid) {
      combined.is_valid = false;
      if (combined.error_message.empty()) {
        combined.error_message = result.error_message;
      } else {
        combined.error_message += "; " + result.error_message;
      }
    }

    // Combine warnings
    combined.warnings.insert(combined.warnings.end(), result.warnings.begin(),
                             result.warnings.end());
  }

  return combined;
}

bool Validator::is_safe_path(const std::string &path) {
  // Check for path traversal attempts
  if (path.find("..") != std::string::npos)
    return false;

  // Check for absolute paths that might be dangerous
  if (path.find("/etc/") == 0 || path.find("/sys/") == 0 ||
      path.find("/proc/") == 0 || path.find("/dev/") == 0) {
    return false;
  }

  // Check for Windows system paths
  if (path.find("C:\\Windows\\") == 0 || path.find("C:\\System32\\") == 0) {
    return false;
  }

  return true;
}

bool Validator::is_text_file_extension(const std::string &extension) {
  static const std::vector<std::string> text_extensions = {
      ".txt", ".md",   ".cpp", ".h",   ".hpp",  ".c",    ".cc",
      ".cxx", ".py",   ".js",  ".ts",  ".html", ".css",  ".json",
      ".xml", ".yaml", ".yml", ".ini", ".cfg",  ".conf", ".log",
      ".sh",  ".bat",  ".ps1", ".sql", ".csv",  ".tsv"};

  std::string lower_ext = extension;
  std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
                 ::tolower);

  return std::find(text_extensions.begin(), text_extensions.end(), lower_ext) !=
         text_extensions.end();
}

std::string Validator::sanitize_input(const std::string &input) {
  std::string sanitized = input;

  // Remove null bytes
  sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\0'),
                  sanitized.end());

  // Trim whitespace
  size_t start = sanitized.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";

  size_t end = sanitized.find_last_not_of(" \t\n\r");
  sanitized = sanitized.substr(start, end - start + 1);

  return sanitized;
}

} // namespace Utils
