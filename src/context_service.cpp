#include "context_service.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Services {

// Static member definitions
std::map<std::string, std::string> ContextService::context_cache;
std::string ContextService::cached_working_directory;

std::string ContextService::load_hierarchical_context(
    const std::string &working_directory) {
  // Check cache first
  if (cached_working_directory == working_directory &&
      context_cache.find("hierarchical") != context_cache.end()) {
    return context_cache["hierarchical"];
  }

  std::vector<ContextFile> all_files = find_context_files(working_directory);
  std::string merged_context = merge_context_files(all_files);

  // Update cache
  cached_working_directory = working_directory;
  context_cache["hierarchical"] = merged_context;

  return merged_context;
}

std::vector<ContextFile>
ContextService::find_context_files(const std::string &working_directory) {
  std::vector<ContextFile> files;

  // 1. Load global context (highest priority)
  ContextFile global_context = load_global_context();
  if (!global_context.content.empty()) {
    files.push_back(global_context);
  }

  // 2. Load project context files (from root to current)
  auto project_files = load_project_context(working_directory);
  files.insert(files.end(), project_files.begin(), project_files.end());

  // 3. Load local context files (current directory and subdirectories)
  auto local_files = load_local_context(working_directory);
  files.insert(files.end(), local_files.begin(), local_files.end());

  // Sort by priority (higher priority first)
  std::sort(files.begin(), files.end(),
            [](const ContextFile &a, const ContextFile &b) {
              return a.priority > b.priority;
            });

  return files;
}

ContextFile ContextService::load_global_context() {
  ContextFile context;
  context.source = "global";
  context.priority = 100; // Highest priority

  std::string home_dir = get_home_directory();
  if (home_dir.empty()) {
    return context;
  }

  std::string global_path = home_dir + "/.agentware/AGENTWARE.md";

  if (is_readable_file(global_path)) {
    context.file_path = global_path;
    context.content = load_file_content(global_path);
  }

  return context;
}

std::vector<ContextFile>
ContextService::load_project_context(const std::string &working_directory) {
  std::vector<ContextFile> files;

  // Find project root
  std::string project_root = find_project_root(working_directory);
  if (project_root.empty()) {
    return files;
  }

  // Walk from project root to current directory
  std::filesystem::path current_path =
      std::filesystem::absolute(working_directory);
  std::filesystem::path root_path = std::filesystem::absolute(project_root);

  std::vector<std::filesystem::path> path_hierarchy;

  // Build path hierarchy from root to current
  std::filesystem::path temp_path = current_path;
  while (temp_path != root_path && temp_path.has_parent_path()) {
    path_hierarchy.push_back(temp_path);
    temp_path = temp_path.parent_path();
  }
  path_hierarchy.push_back(root_path);

  // Reverse to go from root to current
  std::reverse(path_hierarchy.begin(), path_hierarchy.end());

  // Load AGENTWARE.md files in hierarchy order
  int priority = 90; // Start below global priority
  for (const auto &path : path_hierarchy) {
    std::string context_file = path.string() + "/AGENTWARE.md";

    if (is_readable_file(context_file)) {
      ContextFile context;
      context.file_path = context_file;
      context.content = load_file_content(context_file);
      context.priority = priority;
      context.source = "project";

      files.push_back(context);
    }

    priority -= 10; // Decrease priority as we go deeper
  }

  return files;
}

std::vector<ContextFile>
ContextService::load_local_context(const std::string &working_directory) {
  std::vector<ContextFile> files;

  try {
    // Look for AGENTWARE.md files in subdirectories
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(working_directory)) {
      if (entry.is_regular_file() &&
          entry.path().filename() == "AGENTWARE.md") {

        std::string entry_path = entry.path().string();

        // Simple check - if it's in a parent directory, skip
        std::filesystem::path rel_path =
            std::filesystem::relative(entry.path(), working_directory);
        if (rel_path.string().find("..") == 0) {
          continue;
        }

        ContextFile context;
        context.file_path = entry_path;
        context.content = load_file_content(entry_path);
        context.priority = 10; // Lower priority than project files
        context.source = "local";

        files.push_back(context);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error loading local context: " << e.what() << std::endl;
  }

  return files;
}

std::string
ContextService::merge_context_files(const std::vector<ContextFile> &files) {
  if (files.empty()) {
    return "";
  }

  std::ostringstream merged;
  merged << "# Agentware Context\n\n";

  // Group files by source
  std::map<std::string, std::vector<const ContextFile *>> files_by_source;
  for (const auto &file : files) {
    files_by_source[file.source].push_back(&file);
  }

  // Output in order: global, project, local
  std::vector<std::string> source_order = {"global", "project", "local"};

  for (const auto &source : source_order) {
    if (files_by_source.find(source) == files_by_source.end()) {
      continue;
    }

    merged << "## " << source << " Context\n\n";

    for (const auto *file : files_by_source[source]) {
      std::string display_path = get_display_path(file->file_path, ".");
      merged << "### From: " << display_path << "\n\n";
      merged << file->content;

      if (!file->content.empty() && file->content.back() != '\n') {
        merged << "\n";
      }
      merged << "\n";
    }
  }

  return merged.str();
}

std::string
ContextService::find_project_root(const std::string &starting_directory) {
  std::filesystem::path current_path =
      std::filesystem::absolute(starting_directory);

  while (current_path.has_parent_path()) {
    if (is_project_root(current_path.string())) {
      return current_path.string();
    }
    current_path = current_path.parent_path();
  }

  return ""; // No project root found
}

bool ContextService::is_project_root(const std::string &directory) {
  std::vector<std::string> project_markers = {
      ".git",       "package.json", "CMakeLists.txt", "Makefile",
      "Cargo.toml", "pom.xml",      "build.gradle",   "requirements.txt",
      "setup.py",   "go.mod",       ".gitignore"};

  for (const auto &marker : project_markers) {
    std::string marker_path = directory + "/" + marker;
    if (std::filesystem::exists(marker_path)) {
      return true;
    }
  }

  return false;
}

void ContextService::refresh_context_cache() {
  context_cache.clear();
  cached_working_directory.clear();
}

std::string ContextService::get_context_template() {
  return R"(# Agentware Context File

This file provides context and instructions for Agentware AI assistant.

## Project Overview

Brief description of this project/component.

## Key Information

- Important facts about the codebase
- Coding standards and conventions
- Architecture decisions
- Dependencies and requirements

## Instructions for AI

- Specific guidance for AI when working in this context
- Preferred approaches or patterns
- Things to avoid or be careful about

## Examples

```
Example code or commands that are commonly used in this context
```

## References

- Links to documentation
- Related files or components
- External resources
)";
}

bool ContextService::create_context_file(const std::string &directory,
                                         const std::string &content) {
  try {
    std::string file_path = directory + "/AGENTWARE.md";

    // Don't overwrite existing file
    if (std::filesystem::exists(file_path)) {
      return false;
    }

    std::ofstream file(file_path);
    if (!file.is_open()) {
      return false;
    }

    if (content.empty()) {
      file << get_context_template();
    } else {
      file << content;
    }

    return true;
  } catch (const std::exception &) {
    return false;
  }
}

std::string ContextService::get_home_directory() {
  const char *home = std::getenv("HOME");
  if (!home) {
    home = std::getenv("USERPROFILE"); // Windows
  }
  return home ? std::string(home) : "";
}

bool ContextService::is_readable_file(const std::string &file_path) {
  try {
    return std::filesystem::exists(file_path) &&
           std::filesystem::is_regular_file(file_path);
  } catch (const std::exception &) {
    return false;
  }
}

std::string ContextService::load_file_content(const std::string &file_path) {
  try {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      return "";
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
  } catch (const std::exception &) {
    return "";
  }
}

std::string
ContextService::get_display_path(const std::string &file_path,
                                 const std::string &base_directory) {
  try {
    std::filesystem::path rel_path =
        std::filesystem::relative(file_path, base_directory);
    return rel_path.string();
  } catch (const std::exception &) {
    return file_path;
  }
}

} // namespace Services
