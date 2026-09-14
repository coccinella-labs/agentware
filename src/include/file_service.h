#pragma once
#include <string>
#include <vector>

namespace Services {
struct FileSearchResult {
  std::string file_path{};
  int line_number{0};
  std::string line_content{};
  std::string match_context{};
};

struct FileEditResult {
  bool success{};
  std::string message{};
  int replacements_made{0};
};

class FileService {
public:
  // Basic file operations
  static std::string read_file(const std::string &filename);
  static std::string read_file_range(const std::string &filename,
                                     int start_line = 0, int line_count = -1);
  static std::string write_file(const std::string &filename,
                                const std::string &content);
  static bool file_exists(const std::string &filename);
  static std::string get_file_extension(const std::string &filename);

  // Advanced file operations
  static FileEditResult replace_text_in_file(const std::string &filename,
                                             const std::string &old_text,
                                             const std::string &new_text,
                                             int expected_replacements = 1);
  static std::vector<FileSearchResult>
  search_in_file(const std::string &filename, const std::string &pattern,
                 bool case_sensitive = false);
  static std::vector<FileSearchResult>
  search_in_directory(const std::string &directory, const std::string &pattern,
                      const std::string &file_filter = "*",
                      bool case_sensitive = false);

  // Utility functions
  [[nodiscard]] static bool is_text_file(const std::string &filename);
  static std::string get_relative_path(const std::string &filepath,
                                       const std::string &base_path);
  static bool is_within_directory(const std::string &filepath,
                                  const std::string &base_directory);
  static std::string normalize_path(const std::string &path);
};
} // namespace Services
