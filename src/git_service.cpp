#include "git_service.h"
#include "platform.h"
#include "validation.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace Services {

GitService::GitService() {}

namespace {
// The && and quotes in these commands are fixed by the implementation;
// only `path` varies. Reject path content that could break out of the
// double quotes or chain commands, instead of warning on our own
// operators (which made every call fail validation).
bool is_safe_shell_path(const std::string &path) {
  static const std::string forbidden = "\"$`\\;&|<>\n\r";
  return path.find_first_of(forbidden) == std::string::npos;
}
} // namespace

bool GitService::is_git_repository(const std::string &path) {
  std::filesystem::path git_dir = std::filesystem::path(path) / ".git";
  return std::filesystem::exists(git_dir);
}

std::string GitService::get_git_log(const std::string &path, int days) {
  if (!is_git_repository(path)) {
    return "Error: Not a git repository";
  }

  std::string command = "cd \"" + path + "\" && git log --oneline --since=\"" +
                        std::to_string(days) + " days ago\"";

  if (!is_safe_shell_path(path)) {
    return "Error: Unsafe repository path";
  }

  FILE *pipe = Utils::Platform::open_process(command, "r");
  if (!pipe) {
    return "Error: Failed to execute git command";
  }

  std::string result;
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }
  Utils::Platform::close_process(pipe);

  return result.empty()
             ? "No commits found in the last " + std::to_string(days) + " days"
             : result;
}

std::string GitService::get_git_status(const std::string &path) {
  if (!is_git_repository(path)) {
    return "Error: Not a git repository";
  }

  std::string command = "cd \"" + path + "\" && git status --porcelain";

  if (!is_safe_shell_path(path)) {
    return "Error: Unsafe repository path";
  }

  FILE *pipe = Utils::Platform::open_process(command, "r");
  if (!pipe) {
    return "Error: Failed to execute git status";
  }

  std::string result;
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }
  Utils::Platform::close_process(pipe);

  return result.empty() ? "Working directory clean" : result;
}

std::vector<std::string> GitService::get_changed_files(const std::string &path,
                                                       int days) {
  std::vector<std::string> files;

  if (!is_git_repository(path)) {
    return files;
  }

  std::string command =
      "cd \"" + path + "\" && git log --name-only --pretty=format: --since=\"" +
      std::to_string(days) + " days ago\" | sort | uniq";

  if (!is_safe_shell_path(path)) {
    return files;
  }

  FILE *pipe = Utils::Platform::open_process(command, "r");
  if (!pipe) {
    return files;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    std::string file(buffer);
    // Remove newline
    if (!file.empty() && file.back() == '\n') {
      file.pop_back();
    }
    if (!file.empty()) {
      files.push_back(file);
    }
  }
  Utils::Platform::close_process(pipe);

  return files;
}

std::string GitService::analyze_repository(const std::string &path) {
  if (!is_git_repository(path)) {
    return "Error: Not a git repository";
  }

  std::ostringstream analysis;
  analysis << "Git Repository Analysis:\n\n";

  // Get basic info
  std::string branch_cmd = "cd \"" + path + "\" && git branch --show-current";
  if (!is_safe_shell_path(path)) {
    return "Error: Unsafe repository path";
  }
  FILE *pipe = Utils::Platform::open_process(branch_cmd, "r");
  if (pipe) {
    char buffer[128];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string branch(buffer);
      if (!branch.empty() && branch.back() == '\n') {
        branch.pop_back();
      }
      analysis << "Current branch: " << branch << "\n";
    }
    Utils::Platform::close_process(pipe);
  }

  // Get commit count
  std::string count_cmd = "cd \"" + path + "\" && git rev-list --count HEAD";
  pipe = Utils::Platform::open_process(count_cmd, "r");
  if (pipe) {
    char buffer[128];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      analysis << "Total commits: " << buffer;
    }
    Utils::Platform::close_process(pipe);
  }

  // Get recent activity
  analysis << "\nRecent commits (last 7 days):\n";
  analysis << get_git_log(path, 7);

  // Get status
  analysis << "\nWorking directory status:\n";
  analysis << get_git_status(path);

  return analysis.str();
}

} // namespace Services
