#pragma once
#include <map>
#include <string>
#include <vector>

namespace Services {

struct ContextFile {
  std::string file_path{};
  std::string content{};
  int priority{0};      // Higher priority = loaded first
  std::string source{}; // "global", "project", "local"
};

class ContextService {
public:
  // Load hierarchical context from AGENTWARE.md files
  static std::string
  load_hierarchical_context(const std::string &working_directory = ".");

  // Find all AGENTWARE.md files in hierarchy
  static std::vector<ContextFile>
  find_context_files(const std::string &working_directory = ".");

  // Load global context file
  static ContextFile load_global_context();

  // Load project context files (from root to current directory)
  static std::vector<ContextFile>
  load_project_context(const std::string &working_directory);

  // Load local context files (in current directory and subdirectories)
  static std::vector<ContextFile>
  load_local_context(const std::string &working_directory);

  // Merge context files into single string
  static std::string merge_context_files(const std::vector<ContextFile> &files);

  // Find project root (directory with .git, package.json, CMakeLists.txt, etc.)
  static std::string
  find_project_root(const std::string &starting_directory = ".");

  // Check if directory contains project markers
  static bool is_project_root(const std::string &directory);

  // Refresh context cache
  static void refresh_context_cache();

  // Get context file template
  static std::string get_context_template();

  // Create context file in directory
  static bool create_context_file(const std::string &directory,
                                  const std::string &content = "");

private:
  // Cache for loaded context
  static std::map<std::string, std::string> context_cache;
  static std::string cached_working_directory;

  // Get home directory
  static std::string get_home_directory();

  // Check if file exists and is readable
  static bool is_readable_file(const std::string &file_path);

  // Load file content safely
  static std::string load_file_content(const std::string &file_path);

  // Get relative path for display
  static std::string get_display_path(const std::string &file_path,
                                      const std::string &base_directory);
};
} // namespace Services
