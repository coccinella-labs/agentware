#pragma once

#include "web_service.h"
#include <map>
#include <string>
#include <vector>

namespace Services {

struct GitHubIssue {
  int number{0};
  std::string title{};
  std::string body{};
  std::string state{};
  std::vector<std::string> labels{};
  std::vector<std::string> assignees{};
  int milestone{0};
};

struct GitHubPR {
  int number{0};
  std::string title{};
  std::string body{};
  std::string state{};
  std::string head_branch{};
  std::string base_branch{};
  bool mergeable{false};
};

class GitHubService {
private:
  static std::string get_github_token();
  static std::string get_api_base_url();
  static WebResponse make_github_request(const std::string &endpoint,
                                         const std::string &method = "GET",
                                         const std::string &body = "");

public:
  GitHubService() = delete;

  static std::string get_repo_info(const std::string &owner,
                                   const std::string &repo);
  static std::vector<GitHubIssue> get_issues(const std::string &owner,
                                             const std::string &repo,
                                             const std::string &state = "open");
  static GitHubIssue create_issue(const std::string &owner,
                                  const std::string &repo,
                                  const std::string &title,
                                  const std::string &body,
                                  const std::vector<std::string> &labels = {});
  static bool update_issue(const std::string &owner, const std::string &repo,
                           int issue_number,
                           const std::map<std::string, std::string> &updates);
  static bool add_comment(const std::string &owner, const std::string &repo,
                          int issue_number, const std::string &comment);
  static std::vector<GitHubPR>
  get_pull_requests(const std::string &owner, const std::string &repo,
                    const std::string &state = "open");
  static GitHubPR get_pull_request(const std::string &owner,
                                   const std::string &repo, int pr_number);
  static bool create_pr_comment(const std::string &owner,
                                const std::string &repo, int pr_number,
                                const std::string &comment);
  static std::vector<std::string> get_milestones(const std::string &owner,
                                                 const std::string &repo);
  static int create_milestone(const std::string &owner, const std::string &repo,
                              const std::string &title,
                              const std::string &description = "");
  static std::vector<std::string> get_labels(const std::string &owner,
                                             const std::string &repo);
  static bool create_label(const std::string &owner, const std::string &repo,
                           const std::string &name, const std::string &color,
                           const std::string &description = "");
  static bool create_webhook(const std::string &owner, const std::string &repo,
                             const std::string &url,
                             const std::vector<std::string> &events);
  static std::string run_health_check(const std::string &owner,
                                      const std::string &repo);
  static std::string find_related_issues(const std::string &owner,
                                         const std::string &repo,
                                         const std::string &text);
  static bool is_available();
};

} // namespace Services
