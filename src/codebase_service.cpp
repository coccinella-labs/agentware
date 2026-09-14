#include "codebase_service.h"
#include "file_service.h"
#include "git_service.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace Services {

CodebaseService::CodebaseService() {}

std::string CodebaseService::analyze_structure(const std::string &path) {
  std::ostringstream analysis;
  analysis << "Codebase Structure Analysis:\n\n";

  // Check if it's a git repository
  if (GitService::is_git_repository(path)) {
    analysis << "Git Repository: Yes\n";
    analysis << GitService::analyze_repository(path) << "\n";
  } else {
    analysis << "Git Repository: No\n\n";
  }

  // Analyze directory structure
  analysis << "Directory Structure:\n";
  analysis << get_directory_tree(path, 2) << "\n";

  // Analyze file types
  auto file_stats = analyze_file_types(path);
  analysis << "File Type Distribution:\n";
  for (const auto &[ext, count] : file_stats) {
    analysis << "  " << ext << ": " << count << " files\n";
  }

  // Look for common project files
  analysis << "\nProject Configuration Files:\n";
  std::vector<std::string> config_files = {
      "package.json",   "Cargo.toml", "pom.xml",          "build.gradle",
      "CMakeLists.txt", "Makefile",   "requirements.txt", "setup.py",
      "Dockerfile",     ".gitignore", "README.md"};

  for (const auto &file : config_files) {
    if (std::filesystem::exists(std::filesystem::path(path) / file)) {
      analysis << "   " << file << "\n";
    }
  }

  return analysis.str();
}

std::string CodebaseService::find_main_components(const std::string &path) {
  std::ostringstream result;
  result << "Main Components Analysis:\n\n";

  // Look for main directories
  std::vector<std::string> main_dirs;
  for (const auto &entry : std::filesystem::directory_iterator(path)) {
    if (entry.is_directory()) {
      std::string dir_name = entry.path().filename().string();
      if (dir_name[0] != '.' && dir_name != "node_modules" &&
          dir_name != "build" && dir_name != "dist") {
        main_dirs.push_back(dir_name);
      }
    }
  }

  result << "Main Directories:\n";
  for (const auto &dir : main_dirs) {
    result << "  - " << dir << "/\n";

    // Analyze what's in each directory
    std::string dir_path = path + "/" + dir;
    auto file_count = count_files_in_directory(dir_path);
    result << "    Files: " << file_count << "\n";

    // Look for key files
    auto key_files = find_key_files(dir_path);
    if (!key_files.empty()) {
      result << "    Key files: ";
      for (size_t i = 0; i < key_files.size() && i < 3; ++i) {
        if (i > 0)
          result << ", ";
        result << key_files[i];
      }
      if (key_files.size() > 3) {
        result << " (+" << (key_files.size() - 3) << " more)";
      }
      result << "\n";
    }
  }

  return result.str();
}

std::vector<std::string> CodebaseService::find_todos(const std::string &path) {
  std::vector<std::string> todos;

  // Search for TODO, FIXME, HACK comments
  auto search_results =
      FileService::search_in_directory(path, "TODO|FIXME|HACK", "*");

  for (const auto &match : search_results) {
    std::string todo_line = match.file_path + ":" +
                            std::to_string(match.line_number) + ": " +
                            match.line_content;
    todos.push_back(todo_line);
  }

  return todos;
}

std::string CodebaseService::get_directory_tree(const std::string &path,
                                                int max_depth) {
  std::ostringstream tree;
  build_tree_recursive(path, tree, "", 0, max_depth);
  return tree.str();
}

void CodebaseService::build_tree_recursive(const std::string &path,
                                           std::ostringstream &tree,
                                           const std::string &prefix, int depth,
                                           int max_depth) {
  if (depth >= max_depth)
    return;

  std::vector<std::filesystem::directory_entry> entries;
  try {
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      std::string name = entry.path().filename().string();
      if (name[0] != '.' && name != "node_modules" && name != "build" &&
          name != "dist") {
        entries.push_back(entry);
      }
    }
  } catch (const std::exception &) {
    return;
  }

  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    return a.path().filename().string() < b.path().filename().string();
  });

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    bool is_last = (i == entries.size() - 1);

    tree << prefix << (is_last ? "└── " : "├── ")
         << entry.path().filename().string();

    if (entry.is_directory()) {
      tree << "/\n";
      std::string new_prefix = prefix + (is_last ? "    " : "│   ");
      build_tree_recursive(entry.path().string(), tree, new_prefix, depth + 1,
                           max_depth);
    } else {
      tree << "\n";
    }
  }
}

std::map<std::string, int>
CodebaseService::analyze_file_types(const std::string &path) {
  std::map<std::string, int> file_stats;

  try {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(path)) {
      if (entry.is_regular_file()) {
        std::string ext = entry.path().extension().string();
        if (ext.empty()) {
          ext = "[no extension]";
        }
        file_stats[ext]++;
      }
    }
  } catch (const std::exception &) {
    // Handle permission errors, etc.
  }

  return file_stats;
}

int CodebaseService::count_files_in_directory(const std::string &path) {
  int count = 0;
  try {
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      if (entry.is_regular_file()) {
        count++;
      }
    }
  } catch (const std::exception &) {
    // Handle errors
  }
  return count;
}

std::vector<std::string>
CodebaseService::find_key_files(const std::string &path) {
  std::vector<std::string> key_files;

  // Patterns for important files
  std::vector<std::regex> patterns = {
      std::regex(".*main\\.(cpp|c|py|js|ts|java)$",
                 std::regex_constants::icase),
      std::regex(".*index\\.(js|ts|html|php)$", std::regex_constants::icase),
      std::regex(".*app\\.(cpp|c|py|js|ts|java)$", std::regex_constants::icase),
      std::regex(".*server\\.(cpp|c|py|js|ts|java)$",
                 std::regex_constants::icase),
      std::regex(".*client\\.(cpp|c|py|js|ts|java)$",
                 std::regex_constants::icase)};

  try {
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      if (entry.is_regular_file()) {
        std::string filename = entry.path().filename().string();
        for (const auto &pattern : patterns) {
          if (std::regex_match(filename, pattern)) {
            key_files.push_back(filename);
            break;
          }
        }
      }
    }
  } catch (const std::exception &) {
    // Handle errors
  }

  return key_files;
}

} // namespace Services
