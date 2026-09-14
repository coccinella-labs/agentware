#pragma once

#include <string>
#include <vector>

namespace Services {

class GitService {
public:
  GitService();

  // Check if directory is a git repository
  static bool is_git_repository(const std::string &path);

  // Get git log for specified number of days
  static std::string get_git_log(const std::string &path, int days = 7);

  // Get git status
  static std::string get_git_status(const std::string &path);

  // Get list of files changed in last N days
  static std::vector<std::string> get_changed_files(const std::string &path,
                                                    int days = 7);

  // Analyze repository structure and recent activity
  static std::string analyze_repository(const std::string &path);
};

} // namespace Services
