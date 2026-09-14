#include "multi_file_service.h"
#include "platform.h"
#include "validation.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>

namespace Services {

std::vector<FileMatch>
MultiFileService::read_many_files(const std::vector<std::string> &paths,
                                  const MultiFileOptions &options) {
  std::vector<FileMatch> results;
  size_t total_size = 0;

  for (const auto &path : paths) {
    if (results.size() >= options.max_files) {
      break;
    }

    try {
      if (std::filesystem::is_directory(path)) {
        auto dir_files = read_directory_files(path, options);
        for (auto &file : dir_files) {
          if (results.size() >= options.max_files ||
              total_size >= options.max_total_size) {
            break;
          }
          total_size += file.size;
          results.push_back(std::move(file));
        }
      } else if (std::filesystem::is_regular_file(path)) {
        if (should_include_file(path, options)) {
          FileMatch match;
          match.file_path = std::filesystem::path(path).string();
          match.relative_path = std::filesystem::relative(path).string();
          match.file_type = get_file_type(path);

          // Read file content
          std::ifstream file(path);
          if (file.is_open()) {
            std::ostringstream content;
            content << file.rdbuf();
            match.content = content.str();
            match.size = match.content.size();

            if (match.size <= options.max_file_size &&
                total_size + match.size <= options.max_total_size) {
              total_size += match.size;
              results.push_back(std::move(match));
            }
          }
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error processing path " << path << ": " << e.what()
                << std::endl;
    }
  }

  return results;
}

std::vector<FileMatch>
MultiFileService::read_directory_files(const std::string &directory,
                                       const MultiFileOptions &options) {
  std::vector<FileMatch> results;
  size_t total_size = 0;

  // Load gitignore patterns if enabled
  std::set<std::string> gitignore_patterns;
  if (options.respect_gitignore) {
    gitignore_patterns = load_gitignore_patterns(directory);
  }

  // Get Git status filtered files if Git filtering is enabled
  std::vector<std::string> git_filtered_files;
  std::set<std::string> git_files_set;
  if (options.git_filter != GitStatusFilter::ALL) {
    git_filtered_files = get_git_status_files(directory, options.git_filter);
    git_files_set = std::set<std::string>(git_filtered_files.begin(),
                                          git_filtered_files.end());
  }

  try {
    if (options.recursive) {
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(directory)) {
        if (results.size() >= options.max_files ||
            total_size >= options.max_total_size) {
          break;
        }

        if (!entry.is_regular_file()) {
          continue;
        }

        std::string file_path = entry.path().string();

        // Check gitignore patterns
        if (options.respect_gitignore &&
            matches_gitignore(file_path, gitignore_patterns)) {
          continue;
        }

        // Check include/exclude patterns
        if (!should_include_file(file_path, options)) {
          continue;
        }

        // Create file match
        FileMatch match;
        match.file_path = file_path;
        match.relative_path =
            std::filesystem::relative(file_path, directory).string();
        match.file_type = get_file_type(file_path);
        match.size = entry.file_size();

        // Skip if file is too large
        if (match.size > options.max_file_size) {
          continue;
        }

        // Read content
        std::ifstream file(file_path);
        if (file.is_open()) {
          std::ostringstream content;
          content << file.rdbuf();
          match.content = content.str();

          if (total_size + match.size <= options.max_total_size) {
            total_size += match.size;
            results.push_back(std::move(match));
          }
        }
      }
    } else {
      for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (results.size() >= options.max_files ||
            total_size >= options.max_total_size) {
          break;
        }

        if (!entry.is_regular_file()) {
          continue;
        }

        std::string file_path = entry.path().string();

        // Check gitignore patterns
        if (options.respect_gitignore &&
            matches_gitignore(file_path, gitignore_patterns)) {
          continue;
        }

        // Check include/exclude patterns
        if (!should_include_file(file_path, options)) {
          continue;
        }

        // Create file match
        FileMatch match;
        match.file_path = file_path;
        match.relative_path =
            std::filesystem::relative(file_path, directory).string();
        match.file_type = get_file_type(file_path);
        match.size = entry.file_size();

        // Skip if file is too large
        if (match.size > options.max_file_size) {
          continue;
        }

        // Read content
        std::ifstream file(file_path);
        if (file.is_open()) {
          std::ostringstream content;
          content << file.rdbuf();
          match.content = content.str();

          if (total_size + match.size <= options.max_total_size) {
            total_size += match.size;
            results.push_back(std::move(match));
          }
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error reading directory " << directory << ": " << e.what()
              << std::endl;
  }

  return results;
}

bool MultiFileService::should_include_file(const std::string &file_path,
                                           const MultiFileOptions &options) {
  // Check if hidden files should be included
  std::string filename = std::filesystem::path(file_path).filename().string();
  if (!options.include_hidden_files && !filename.empty() &&
      filename[0] == '.') {
    return false;
  }

  // Check if only text files should be included
  if (options.only_text_files && !is_text_file(file_path)) {
    return false;
  }

  // Check language filters
  if (!matches_language_filter(file_path, options.language_filters)) {
    return false;
  }

  // Check exclude patterns first
  for (const auto &pattern : options.exclude_patterns) {
    if (matches_pattern(file_path, pattern)) {
      return false;
    }
  }

  // Check default exclude patterns
  auto default_excludes = get_default_exclude_patterns();
  for (const auto &pattern : default_excludes) {
    if (matches_pattern(file_path, pattern)) {
      return false;
    }
  }

  // If no include patterns specified, include by default
  if (options.include_patterns.empty()) {
    return true;
  }

  // Check include patterns
  for (const auto &pattern : options.include_patterns) {
    if (matches_pattern(file_path, pattern)) {
      return true;
    }
  }

  return false;
}

std::string MultiFileService::get_file_type(const std::string &file_path) {
  std::string ext = std::filesystem::path(file_path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
      ext == ".h" || ext == ".hpp" || ext == ".hxx") {
    return "C++";
  } else if (ext == ".py") {
    return "Python";
  } else if (ext == ".js" || ext == ".ts") {
    return "JavaScript/TypeScript";
  } else if (ext == ".java") {
    return "Java";
  } else if (ext == ".rs") {
    return "Rust";
  } else if (ext == ".go") {
    return "Go";
  } else if (ext == ".md") {
    return "Markdown";
  } else if (ext == ".json") {
    return "JSON";
  } else if (ext == ".yaml" || ext == ".yml") {
    return "YAML";
  } else if (ext == ".xml") {
    return "XML";
  } else if (ext == ".txt") {
    return "Text";
  } else if (ext == ".sh" || ext == ".bash") {
    return "Shell Script";
  } else {
    return "Unknown";
  }
}

std::set<std::string>
MultiFileService::load_gitignore_patterns(const std::string &directory) {
  std::set<std::string> patterns;

  // Add common patterns
  patterns.insert("node_modules/");
  patterns.insert(".git/");
  patterns.insert("build/");
  patterns.insert("dist/");
  patterns.insert(".cache/");
  patterns.insert("*.log");
  patterns.insert("*.tmp");
  patterns.insert("*.swp");
  patterns.insert(".DS_Store");

  // Try to read .gitignore file
  std::string gitignore_path = directory + "/.gitignore";
  std::ifstream gitignore_file(gitignore_path);

  if (gitignore_file.is_open()) {
    std::string line;
    while (std::getline(gitignore_file, line)) {
      // Skip empty lines and comments
      if (line.empty() || line[0] == '#') {
        continue;
      }

      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t"));
      line.erase(line.find_last_not_of(" \t") + 1);

      if (!line.empty()) {
        patterns.insert(line);
      }
    }
  }

  return patterns;
}

bool MultiFileService::matches_gitignore(
    const std::string &file_path, const std::set<std::string> &patterns) {
  for (const auto &pattern : patterns) {
    if (matches_pattern(file_path, pattern)) {
      return true;
    }
  }
  return false;
}

std::string MultiFileService::format_multi_file_content(
    const std::vector<FileMatch> &files,
    const std::string &context_description) {
  std::ostringstream formatted;

  if (!context_description.empty()) {
    formatted << "[" << context_description << "]\n\n";
  }

  formatted << "[Multi-file content: " << files.size() << " files]\n\n";

  // Group files by type
  std::map<std::string, std::vector<const FileMatch *>> files_by_type;
  for (const auto &file : files) {
    files_by_type[file.file_type].push_back(&file);
  }

  // Output summary
  formatted << "File Summary:\n";
  for (const auto &[type, type_files] : files_by_type) {
    formatted << "  " << type << ": " << type_files.size() << " files\n";
  }
  formatted << "\n";

  // Output file contents
  for (const auto &file : files) {
    formatted << "=== " << file.relative_path << " (" << file.file_type
              << ") ===\n";
    formatted << file.content;
    if (!file.content.empty() && file.content.back() != '\n') {
      formatted << "\n";
    }
    formatted << "\n";
  }

  return formatted.str();
}

std::vector<std::string> MultiFileService::get_default_exclude_patterns() {
  return {
      "*.exe",    "*.dll",    "*.so",  "*.dylib",        "*.a",    "*.lib",
      "*.obj",    "*.o",      "*.png", "*.jpg",          "*.jpeg", "*.gif",
      "*.bmp",    "*.ico",    "*.svg", "*.mp3",          "*.mp4",  "*.avi",
      "*.mov",    "*.wav",    "*.pdf", "*.zip",          "*.tar",  "*.gz",
      "*.7z",     "*.rar",    "*.bin", "node_modules/*", ".git/*", "build/*",
      "dist/*",   ".cache/*", "*.log", "*.tmp",          "*.swp",  ".DS_Store",
      "Thumbs.db"};
}

bool MultiFileService::matches_pattern(const std::string &file_path,
                                       const std::string &pattern) {
  // Simple glob-like pattern matching
  // Convert glob pattern to regex
  std::string regex_pattern = pattern;

  // Escape special regex characters except * and ?
  std::regex special_chars(R"([\.\+\^\$\(\)\[\]\{\}\|\\])");
  regex_pattern = std::regex_replace(regex_pattern, special_chars, R"(\$&)");

  // Convert glob wildcards to regex
  std::regex glob_star(R"(\\\*)");
  regex_pattern = std::regex_replace(regex_pattern, glob_star, ".*");

  std::regex glob_question(R"(\\\?)");
  regex_pattern = std::regex_replace(regex_pattern, glob_question, ".");

  // Add anchors for full match
  regex_pattern = "^" + regex_pattern + "$";

  try {
    std::regex pattern_regex(regex_pattern, std::regex_constants::icase);
    return std::regex_match(file_path, pattern_regex) ||
           std::regex_match(
               std::filesystem::path(file_path).filename().string(),
               pattern_regex);
  } catch (const std::exception &) {
    // If regex fails, fall back to simple string matching
    return file_path.find(pattern) != std::string::npos;
  }
}

std::vector<std::string>
MultiFileService::get_git_status_files(const std::string &directory,
                                       GitStatusFilter filter) {
  std::vector<std::string> files;

  try {
    std::string command;
    switch (filter) {
    case GitStatusFilter::TRACKED:
      command = "git ls-files";
      break;
    case GitStatusFilter::UNTRACKED:
      command = "git ls-files --others --exclude-standard";
      break;
    case GitStatusFilter::MODIFIED:
      command = "git diff --name-only HEAD";
      break;
    case GitStatusFilter::STAGED:
      command = "git diff --name-only --cached";
      break;
    case GitStatusFilter::UNSTAGED:
      command = "git diff --name-only";
      break;
    default: // GitStatusFilter::ALL
      command = "git ls-files && git ls-files --others --exclude-standard";
      break;
    }

    // Change to directory and execute git command
    std::string full_command = "cd \"" + directory + "\" && " + command;
    FILE *pipe = Utils::Platform::open_process(full_command, "r");
    if (!pipe)
      return files;

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }
    Utils::Platform::close_process(pipe);

    // Parse output into file paths
    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty()) {
        // Convert to absolute path
        std::string full_path = directory + "/" + line;
        files.push_back(full_path);
      }
    }

  } catch ([[maybe_unused]] const std::exception &e) {
    // If git commands fail, return empty vector
  }

  return files;
}

bool MultiFileService::is_text_file(const std::string &file_path) {
  // Check by extension first
  std::string ext = std::filesystem::path(file_path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  // Common text file extensions
  std::set<std::string> text_extensions = {
      ".txt",   ".md",     ".rst",   ".log",  ".cfg",        ".conf",
      ".ini",   ".yaml",   ".yml",   ".json", ".xml",        ".html",
      ".htm",   ".css",    ".js",    ".ts",   ".jsx",        ".tsx",
      ".c",     ".cpp",    ".cc",    ".cxx",  ".h",          ".hpp",
      ".hxx",   ".java",   ".py",    ".rb",   ".go",         ".rs",
      ".php",   ".pl",     ".sh",    ".bash", ".zsh",        ".fish",
      ".ps1",   ".sql",    ".r",     ".m",    ".swift",      ".kt",
      ".scala", ".clj",    ".hs",    ".ml",   ".dockerfile", ".makefile",
      ".cmake", ".gradle", ".maven", ".sbt"};

  if (text_extensions.count(ext)) {
    return true;
  }

  // Check files without extensions or with unknown extensions
  try {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
      return false;

    // Read first 512 bytes to check for binary content
    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytes_read = file.gcount();

    // Check for null bytes (common in binary files)
    for (std::streamsize i = 0; i < bytes_read; ++i) {
      if (buffer[i] == '\0') {
        return false;
      }
    }

    return true;

  } catch ([[maybe_unused]] const std::exception &e) {
    return false;
  }
}

std::string
MultiFileService::get_language_from_extension(const std::string &file_path) {
  std::string ext = std::filesystem::path(file_path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  // Map extensions to language names
  static std::map<std::string, std::string> ext_to_lang = {
      {".c", "c"},
      {".h", "c"},
      {".cpp", "cpp"},
      {".cc", "cpp"},
      {".cxx", "cpp"},
      {".hpp", "cpp"},
      {".hxx", "cpp"},
      {".py", "python"},
      {".js", "javascript"},
      {".jsx", "javascript"},
      {".ts", "typescript"},
      {".tsx", "typescript"},
      {".java", "java"},
      {".go", "go"},
      {".rs", "rust"},
      {".rb", "ruby"},
      {".php", "php"},
      {".swift", "swift"},
      {".kt", "kotlin"},
      {".scala", "scala"},
      {".clj", "clojure"},
      {".hs", "haskell"},
      {".ml", "ocaml"},
      {".r", "r"},
      {".m", "matlab"},
      {".pl", "perl"},
      {".sh", "shell"},
      {".bash", "shell"},
      {".zsh", "shell"},
      {".fish", "shell"},
      {".ps1", "powershell"},
      {".sql", "sql"},
      {".html", "html"},
      {".htm", "html"},
      {".css", "css"},
      {".xml", "xml"},
      {".json", "json"},
      {".yaml", "yaml"},
      {".yml", "yaml"},
      {".md", "markdown"},
      {".rst", "restructuredtext"},
      {".tex", "latex"},
      {".dockerfile", "docker"},
      {".makefile", "make"},
      {".cmake", "cmake"}};

  auto it = ext_to_lang.find(ext);
  return (it != ext_to_lang.end()) ? it->second : "unknown";
}

bool MultiFileService::matches_language_filter(
    const std::string &file_path,
    const std::vector<std::string> &language_filters) {
  if (language_filters.empty()) {
    return true; // No filter means include all
  }

  std::string file_language = get_language_from_extension(file_path);

  for (const auto &filter : language_filters) {
    std::string filter_lower = filter;
    std::transform(filter_lower.begin(), filter_lower.end(),
                   filter_lower.begin(), ::tolower);

    if (file_language == filter_lower) {
      return true;
    }
  }

  return false;
}

} // namespace Services
