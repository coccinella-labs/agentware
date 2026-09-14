#include "file_service.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#include <algorithm>
#include <cctype>
#include <regex>

namespace Services {
static const std::size_t MAX_FILE_SIZE = 5 * 1024 * 1024; // 5 MB limit

std::string FileService::read_file(const std::string &filename) {
  try {
    if (!file_exists(filename)) {
      return "Error: File '" + filename + "' does not exist";
    }

    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      return "Error: Could not open file '" + filename + "'";
    }

    auto size = file.tellg();
    if (size > static_cast<std::streampos>(MAX_FILE_SIZE)) {
      return "Error: File too large to read (" + std::to_string(size) +
             " bytes)";
    }
    file.seekg(0);

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  } catch ([[maybe_unused]] const std::exception &e) {
    return std::string("Error reading file: ") + e.what();
  }
}

std::string FileService::read_file_range(const std::string &filename,
                                         int start_line, int line_count) {
  try {
    if (!file_exists(filename)) {
      return "Error: File '" + filename + "' does not exist";
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
      return "Error: Could not open file '" + filename + "'";
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
      lines.push_back(line);
    }

    if (start_line < 0 || start_line >= static_cast<int>(lines.size())) {
      return "Error: Start line out of range";
    }

    int end_line =
        (line_count == -1)
            ? static_cast<int>(lines.size())
            : std::min(start_line + line_count, static_cast<int>(lines.size()));

    std::ostringstream result;
    for (int i = start_line; i < end_line; ++i) {
      result << lines[i];
      if (i < end_line - 1)
        result << "\n";
    }

    return result.str();
  } catch ([[maybe_unused]] const std::exception &e) {
    return std::string("Error reading file range: ") + e.what();
  }
}

std::string FileService::write_file(const std::string &filename,
                                    const std::string &content) {
  try {
    if (content.size() > MAX_FILE_SIZE) {
      return "Error: Content too large to write";
    }

    std::filesystem::path file_path(filename);
    if (file_path.has_parent_path()) {
      std::filesystem::create_directories(file_path.parent_path());
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      return "Error: Could not create/write file '" + filename + "'";
    }

    file << content;
    if (!file) {
      return "Error: Write operation failed";
    }
    return "File '" + filename + "' written successfully (" +
           std::to_string(content.length()) + " bytes)";
  } catch ([[maybe_unused]] const std::exception &e) {
    return std::string("Error writing file: ") + e.what();
  }
}

FileEditResult FileService::replace_text_in_file(const std::string &filename,
                                                 const std::string &old_text,
                                                 const std::string &new_text,
                                                 int expected_replacements) {
  FileEditResult result = {false, "", 0};

  try {
    if (!file_exists(filename)) {
      result.message = "Error: File '" + filename + "' does not exist";
      return result;
    }

    // Read file content
    std::ifstream file(filename);
    if (!file.is_open()) {
      result.message = "Error: Could not open file '" + filename + "'";
      return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Count occurrences
    size_t pos = 0;
    int count = 0;
    while ((pos = content.find(old_text, pos)) != std::string::npos) {
      count++;
      pos += old_text.length();
    }

    if (count == 0) {
      result.message = "Error: Text to replace not found in file";
      return result;
    }

    if (expected_replacements > 0 && count != expected_replacements) {
      result.message = "Error: Expected " +
                       std::to_string(expected_replacements) +
                       " replacements but found " + std::to_string(count);
      return result;
    }

    // Perform replacements
    pos = 0;
    while ((pos = content.find(old_text, pos)) != std::string::npos) {
      content.replace(pos, old_text.length(), new_text);
      pos += new_text.length();
    }

    // Write back to file
    std::ofstream out_file(filename);
    if (!out_file.is_open()) {
      result.message = "Error: Could not write to file '" + filename + "'";
      return result;
    }

    out_file << content;
    if (!out_file) {
      result.message = "Error: Write operation failed";
      return result;
    }

    result.success = true;
    result.replacements_made = count;
    result.message =
        "Successfully replaced " + std::to_string(count) + " occurrence(s)";
    return result;

  } catch ([[maybe_unused]] const std::exception &e) {
    result.message = std::string("Error during text replacement: ") + e.what();
    return result;
  }
}

std::vector<FileSearchResult>
FileService::search_in_file(const std::string &filename,
                            const std::string &pattern, bool case_sensitive) {
  std::vector<FileSearchResult> results;

  try {
    if (!file_exists(filename) || !is_text_file(filename)) {
      return results;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
      return results;
    }

    std::regex regex_pattern;
    if (case_sensitive) {
      regex_pattern = std::regex(pattern);
    } else {
      regex_pattern = std::regex(pattern, std::regex_constants::icase);
    }

    std::string line;
    int line_number = 1;

    while (std::getline(file, line)) {
      std::smatch match;
      if (std::regex_search(line, match, regex_pattern)) {
        FileSearchResult result;
        result.file_path = filename;
        result.line_number = line_number;
        result.line_content = line;
        result.match_context = line; // For now, context is the same as line
        results.push_back(result);
      }
      line_number++;
    }

  } catch ([[maybe_unused]] const std::exception &e) {
    // Log error but return partial results
  }

  return results;
}

std::vector<FileSearchResult> FileService::search_in_directory(
    const std::string &directory, const std::string &pattern,
    const std::string &file_filter, bool case_sensitive) {
  std::vector<FileSearchResult> all_results;

  try {
    if (!std::filesystem::exists(directory) ||
        !std::filesystem::is_directory(directory)) {
      return all_results;
    }

    // Simple file filter matching (supports * wildcard)
    auto matches_filter = [&file_filter](const std::string &filename) {
      if (file_filter == "*")
        return true;

      // Simple wildcard matching
      if (file_filter.find('*') != std::string::npos) {
        std::string pattern_regex = file_filter;
        std::replace(pattern_regex.begin(), pattern_regex.end(), '*', '.');
        pattern_regex = ".*" + pattern_regex + ".*";
        try {
          std::regex filter_regex(pattern_regex, std::regex_constants::icase);
          return std::regex_match(filename, filter_regex);
        } catch (...) {
          return false;
        }
      }

      return filename.find(file_filter) != std::string::npos;
    };

    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        std::string filepath = entry.path().string();
        std::string filename = entry.path().filename().string();

        if (matches_filter(filename) && is_text_file(filepath)) {
          auto file_results = search_in_file(filepath, pattern, case_sensitive);
          all_results.insert(all_results.end(), file_results.begin(),
                             file_results.end());
        }
      }
    }

  } catch ([[maybe_unused]] const std::exception &e) {
    // Log error but return partial results
  }

  return all_results;
}

bool FileService::file_exists(const std::string &filename) {
  try {
    return std::filesystem::exists(filename);
  } catch (...) {
    return false;
  }
}

std::string FileService::get_file_extension(const std::string &filename) {
  std::filesystem::path file_path(filename);
  return file_path.extension().string();
}

bool FileService::is_text_file(const std::string &filename) {
  std::string ext = get_file_extension(filename);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  // Common text file extensions
  static const std::vector<std::string> text_extensions = {
      ".txt", ".md",   ".cpp", ".h",   ".hpp",  ".c",    ".cc",
      ".cxx", ".py",   ".js",  ".ts",  ".html", ".css",  ".json",
      ".xml", ".yaml", ".yml", ".ini", ".cfg",  ".conf", ".log",
      ".sh",  ".bat",  ".ps1", ".sql", ".csv",  ".tsv"};

  return std::find(text_extensions.begin(), text_extensions.end(), ext) !=
         text_extensions.end();
}

std::string FileService::get_relative_path(const std::string &filepath,
                                           const std::string &base_path) {
  try {
    std::filesystem::path file_path = std::filesystem::absolute(filepath);
    std::filesystem::path base = std::filesystem::absolute(base_path);
    return std::filesystem::relative(file_path, base).string();
  } catch (...) {
    return filepath;
  }
}

bool FileService::is_within_directory(const std::string &filepath,
                                      const std::string &base_directory) {
  try {
    std::filesystem::path file_path = std::filesystem::absolute(filepath);
    std::filesystem::path base = std::filesystem::absolute(base_directory);

    auto file_it = file_path.begin();
    auto base_it = base.begin();

    while (base_it != base.end() && file_it != file_path.end()) {
      if (*base_it != *file_it) {
        return false;
      }
      ++base_it;
      ++file_it;
    }

    return base_it == base.end();
  } catch (...) {
    return false;
  }
}

std::string FileService::normalize_path(const std::string &path) {
  try {
    return std::filesystem::absolute(path).string();
  } catch (...) {
    return path;
  }
}

} // namespace Services
