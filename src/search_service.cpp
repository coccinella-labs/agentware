#include "search_service.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

#include "file_service.h"
#include "platform.h"

namespace Services {
namespace {

// Directories never walked by the native fallback, even without a .gitignore.
// These are build outputs, VCS metadata, or dependency trees: searching them
// wastes tokens and memory without helping.
const std::vector<std::string> &heavy_dirs() {
  static const std::vector<std::string> dirs = {
      ".git", "node_modules", "target",      "dist",  "build",
      ".hg",  ".svn",         "__pycache__", ".idea", ".vscode",
  };
  return dirs;
}

// Same single-quote idiom as SandboxService::escape_shell_arg: wrap in quotes,
// escape embedded quotes. Used only for arguments this module constructs
// (binary path, flags, pattern, directory); user text never becomes a command.
std::string shell_quote(const std::string &arg) {
  std::string escaped = arg;
  size_t pos = 0;
  while ((pos = escaped.find("'", pos)) != std::string::npos) {
    escaped.replace(pos, 1, "'\"'\"'");
    pos += 5;
  }
  return "'" + escaped + "'";
}

bool is_heavy_dir(const std::string &name,
                  const std::set<std::string> &ignored) {
  for (const auto &d : heavy_dirs()) {
    if (name == d)
      return true;
  }
  return ignored.count(name) > 0;
}

// Reads at most the simple cases of .gitignore in one directory: blank lines,
// comments, negations, and glob patterns are skipped; a trailing slash marks a
// directory name to skip. This is deliberately not a full gitignore engine:
// the fixed heavy-dir list above carries the guarantee, this only adds
// project-specific directory skips.
std::set<std::string> read_gitignore_dirs(const std::string &directory) {
  std::set<std::string> ignored;
  std::ifstream file(directory + "/.gitignore");
  if (!file.is_open())
    return ignored;
  std::string line;
  while (std::getline(file, line)) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
      line.pop_back();
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
      continue;
    line = line.substr(start);
    if (line.empty() || line[0] == '#' || line[0] == '!')
      continue;
    if (line.find_first_of("*?[\\") != std::string::npos)
      continue;
    if (line.back() != '/')
      continue;
    std::string name = line.substr(0, line.size() - 1);
    if (name.find('/') == std::string::npos && !name.empty())
      ignored.insert(name);
  }
  return ignored;
}

bool matches_glob(const std::string &filename, const std::string &glob) {
  if (glob == "*" || glob.empty())
    return true;
  if (glob.find('*') != std::string::npos ||
      glob.find('?') != std::string::npos) {
    std::string pattern_regex;
    for (char c : glob) {
      if (c == '*')
        pattern_regex += ".*";
      else if (c == '?')
        pattern_regex += ".";
      else if (std::string(".^$|()[]{}+\\").find(c) != std::string::npos)
        pattern_regex += std::string("\\") + c;
      else
        pattern_regex += c;
    }
    try {
      return std::regex_match(
          filename, std::regex(pattern_regex, std::regex_constants::icase));
    } catch (...) {
      return false;
    }
  }
  return filename.find(glob) != std::string::npos;
}

bool executable_on_path(const std::filesystem::path &p) {
#ifdef _WIN32
  return std::filesystem::is_regular_file(p);
#else
  namespace fs = std::filesystem;
  if (!fs::is_regular_file(p))
    return false;
  auto perms = fs::status(p).permissions();
  return (perms & (fs::perms::owner_exec | fs::perms::group_exec |
                   fs::perms::others_exec)) != fs::perms::none;
#endif
}

std::string find_on_path_in(const std::string &path_env,
                            const std::string &name) {
#ifdef _WIN32
  const std::vector<std::string> suffixes = {"", ".exe", ".bat", ".cmd"};
  const char separator = ';';
#else
  const std::vector<std::string> suffixes = {""};
  const char separator = ':';
#endif
  size_t start = 0;
  while (start <= path_env.size()) {
    size_t end = path_env.find(separator, start);
    if (end == std::string::npos)
      end = path_env.size();
    std::string dir = path_env.substr(start, end - start);
    if (!dir.empty()) {
      for (const auto &suffix : suffixes) {
        std::filesystem::path candidate =
            std::filesystem::path(dir) / (name + suffix);
        if (executable_on_path(candidate))
          return candidate.string();
      }
    }
    if (end == path_env.size())
      break;
    start = end + 1;
  }
  return "";
}

// Runs a constructed command and captures stdout up to max_bytes. The command
// is built by this module from fixed flags plus shell-quoted arguments; it
// never passes user text to a shell unquoted.
std::string run_capture(const std::string &command, std::size_t max_bytes,
                        bool &timed_out) {
  timed_out = false;
  std::string output;
  FILE *pipe = Utils::Platform::open_process(command.c_str(), "r");
  if (!pipe)
    return output;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
    if (output.size() > max_bytes) {
      timed_out = true;
      break;
    }
  }
  Utils::Platform::close_process(pipe);
  return output;
}

} // namespace

SearchCapabilities detect_search_capabilities() {
  const char *path_env = std::getenv("PATH");
  return detect_search_capabilities_from_path(path_env ? path_env : "");
}

SearchCapabilities
detect_search_capabilities_from_path(const std::string &path_env) {
  SearchCapabilities caps;
  caps.has_ripgrep = !find_on_path_in(path_env, "rg").empty();
  std::string fd = find_on_path_in(path_env, "fd");
  if (fd.empty())
    fd = find_on_path_in(path_env, "fdfind");
  if (!fd.empty()) {
    caps.has_fd = true;
    caps.fd_binary = fd;
  }
  caps.has_ast_grep = !find_on_path_in(path_env, "sg").empty();
  caps.has_jq = !find_on_path_in(path_env, "jq").empty();
  caps.has_git = !find_on_path_in(path_env, "git").empty();
  return caps;
}

CodeSearchResult search_code(const SearchCapabilities &caps,
                             const SearchOptions &options) {
  CodeSearchResult result;

  if (options.pattern.empty())
    return result;

  if (caps.has_ripgrep) {
    result.backend_used = "ripgrep";
    // --vimgrep emits path:line:col:text, trivially parseable without a JSON
    // library. --max-count bounds per-file output; the total cap below bounds
    // the whole result. ripgrep skips non-regular files on its own.
    std::string command = "rg --vimgrep --no-heading --max-count " +
                          std::to_string(options.max_matches);
    // Options must precede the "--" separator: anything after it is a path,
    // so appending -g/--ignore-case there silently disables filtering.
    if (options.file_filter != "*")
      command += " -g " + shell_quote(options.file_filter);
    if (!options.case_sensitive)
      command += " --ignore-case";
    command += " -e " + shell_quote(options.pattern) + " -- " +
               shell_quote(options.directory);

    bool truncated = false;
    std::string output = run_capture(command, options.max_bytes, truncated);
    result.truncated = truncated;

    std::size_t bytes = 0;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
      if (result.matches.size() >= options.max_matches) {
        result.truncated = true;
        break;
      }
      // path:line:col:text — split the first three fields, the rest is text
      // (filenames containing ':' are a known accepted limitation).
      size_t first = line.find(':');
      size_t second =
          (first == std::string::npos) ? first : line.find(':', first + 1);
      size_t third =
          (second == std::string::npos) ? second : line.find(':', second + 1);
      if (first == std::string::npos || second == std::string::npos ||
          third == std::string::npos)
        continue;
      FileSearchResult match;
      match.file_path = line.substr(0, first);
      try {
        match.line_number =
            std::stoi(line.substr(first + 1, second - first - 1));
      } catch (...) {
        continue;
      }
      match.line_content = line.substr(third + 1);
      bytes += match.file_path.size() + match.line_content.size();
      if (bytes > options.max_bytes) {
        result.truncated = true;
        break;
      }
      result.matches.push_back(std::move(match));
    }
    if (truncated)
      result.truncated = true;
    return result;
  }

  result.backend_used = "native";
  std::error_code ec;
  if (!std::filesystem::is_directory(options.directory, ec) || ec)
    return result;

  std::set<std::string> ignored = read_gitignore_dirs(options.directory);
  std::size_t bytes = 0;

  std::function<void(const std::filesystem::path &)> walk =
      [&](const std::filesystem::path &dir) {
        if (result.truncated)
          return;
        std::error_code iter_ec;
        for (const auto &entry : std::filesystem::directory_iterator(
                 dir,
                 std::filesystem::directory_options::skip_permission_denied,
                 iter_ec)) {
          if (iter_ec || result.truncated)
            return;
          std::string name = entry.path().filename().string();
          bool is_dir = entry.is_directory(iter_ec);
          if (iter_ec)
            continue;
          if (is_dir) {
            if (is_heavy_dir(name, ignored))
              continue;
            walk(entry.path());
            continue;
          }
          if (!entry.is_regular_file(iter_ec) || iter_ec)
            continue;
          if (!matches_glob(name, options.file_filter))
            continue;
          ++result.files_scanned;
          auto file_results = FileService::search_in_file(
              entry.path().string(), options.pattern, options.case_sensitive);
          for (auto &match : file_results) {
            if (result.matches.size() >= options.max_matches) {
              result.truncated = true;
              return;
            }
            bytes += match.file_path.size() + match.line_content.size();
            if (bytes > options.max_bytes) {
              result.truncated = true;
              return;
            }
            result.matches.push_back(std::move(match));
          }
        }
      };
  walk(options.directory);
  return result;
}

std::vector<std::string> find_files(const SearchCapabilities &caps,
                                    const std::string &directory,
                                    const std::string &glob,
                                    std::size_t max_results) {
  std::vector<std::string> found;

  if (caps.has_fd && !caps.fd_binary.empty()) {
    std::string command = shell_quote(caps.fd_binary) +
                          " --type f --max-results " +
                          std::to_string(max_results) + " --glob " +
                          shell_quote(glob) + " -- " + shell_quote(directory);
    bool truncated = false;
    std::string output = run_capture(command, max_results * 512, truncated);
    (void)truncated;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line) && found.size() < max_results) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (!line.empty())
        found.push_back(line);
    }
    return found;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(directory, ec) || ec)
    return found;
  std::set<std::string> ignored = read_gitignore_dirs(directory);

  std::function<void(const std::filesystem::path &)> walk =
      [&](const std::filesystem::path &dir) {
        if (found.size() >= max_results)
          return;
        std::error_code iter_ec;
        for (const auto &entry : std::filesystem::directory_iterator(
                 dir,
                 std::filesystem::directory_options::skip_permission_denied,
                 iter_ec)) {
          if (iter_ec || found.size() >= max_results)
            return;
          std::string name = entry.path().filename().string();
          bool is_dir = entry.is_directory(iter_ec);
          if (iter_ec)
            continue;
          if (is_dir) {
            if (is_heavy_dir(name, ignored))
              continue;
            walk(entry.path());
            continue;
          }
          if (entry.is_regular_file(iter_ec) && !iter_ec &&
              matches_glob(name, glob))
            found.push_back(entry.path().string());
        }
      };
  walk(directory);
  return found;
}

std::string search_tool_prompt(const SearchCapabilities &caps) {
  std::string prompt = "CODE SEARCH TOOLS AVAILABLE:\n";
  if (caps.has_ripgrep) {
    prompt += "- content search: ripgrep (rg) is installed. Prefer "
              "`rg --vimgrep -e '<pattern>' <directory>` for fast code "
              "search; results arrive as path:line:column:text.\n";
  } else {
    prompt += "- content search: no ripgrep on PATH. Use the built-in native "
              "search instead, which walks the tree while skipping heavy "
              "directories; expect it to be slower on large trees.\n";
  }
  if (caps.has_fd) {
    prompt += "- file discovery: fd is installed (" + caps.fd_binary +
              "). Prefer `fd --type f --glob '<pattern>' <directory>` for "
              "finding files by name.\n";
  } else {
    prompt += "- file discovery: no fd on PATH. Use the built-in native file "
              "walk instead.\n";
  }
  if (caps.has_ast_grep)
    prompt += "- structural search: ast-grep (sg) is installed for "
              "syntax-aware queries.\n";
  if (caps.has_jq)
    prompt += "- jq is installed for JSON processing.\n";
  if (caps.has_git)
    prompt += "- git is installed for repository inspection.\n";
  prompt += "Search output is capped; a result marked truncated means matches "
            "were cut off, so narrow the pattern or directory and search again "
            "rather than assuming the list is complete.\n";
  return prompt;
}

} // namespace Services
