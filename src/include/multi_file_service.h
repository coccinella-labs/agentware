#pragma once
#include <set>
#include <string>
#include <vector>

namespace Services {

struct FileMatch {
  std::string file_path{};
  std::string relative_path{};
  std::string content{};
  size_t size{0};
  std::string file_type{};
};

enum class GitStatusFilter {
  ALL,       // All files
  TRACKED,   // Only tracked files
  UNTRACKED, // Only untracked files
  MODIFIED,  // Only modified files
  STAGED,    // Only staged files
  UNSTAGED   // Only unstaged changes
};

struct MultiFileOptions {
  std::vector<std::string> include_patterns;
  std::vector<std::string> exclude_patterns;
  bool respect_gitignore = true;
  bool recursive = true;
  size_t max_files = 100;
  size_t max_file_size = 1024 * 1024;       // 1MB
  size_t max_total_size = 10 * 1024 * 1024; // 10MB

  // Advanced Git filtering
  GitStatusFilter git_filter = GitStatusFilter::ALL;
  bool only_text_files = false;
  bool include_hidden_files = false;
  std::vector<std::string>
      language_filters; // e.g., "cpp", "python", "javascript"
};

class MultiFileService {
public:
  // Read multiple files based on patterns
  static std::vector<FileMatch>
  read_many_files(const std::vector<std::string> &paths,
                  const MultiFileOptions &options = MultiFileOptions{});

  // Read files from directory with patterns
  static std::vector<FileMatch>
  read_directory_files(const std::string &directory,
                       const MultiFileOptions &options = MultiFileOptions{});

  // Check if file should be included based on patterns
  static bool should_include_file(const std::string &file_path,
                                  const MultiFileOptions &options);

  // Get file type from extension
  static std::string get_file_type(const std::string &file_path);

  // Load gitignore patterns
  static std::set<std::string>
  load_gitignore_patterns(const std::string &directory);

  // Check if path matches gitignore patterns
  static bool matches_gitignore(const std::string &file_path,
                                const std::set<std::string> &patterns);

  // Format multi-file content for AI
  static std::string
  format_multi_file_content(const std::vector<FileMatch> &files,
                            const std::string &context_description = "");

  // Advanced Git-aware filtering
  static std::vector<std::string>
  get_git_status_files(const std::string &directory, GitStatusFilter filter);

  static bool is_text_file(const std::string &file_path);
  static bool
  matches_language_filter(const std::string &file_path,
                          const std::vector<std::string> &language_filters);

  static std::string get_language_from_extension(const std::string &file_path);

private:
  // Default exclude patterns
  static std::vector<std::string> get_default_exclude_patterns();

  // Check if file matches pattern (supports glob-like patterns)
  static bool matches_pattern(const std::string &file_path,
                              const std::string &pattern);
};
} // namespace Services
