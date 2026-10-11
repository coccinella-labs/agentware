#pragma once

#include "web_service.h"
#include <functional>
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

// One inline review comment on a pull request. Anchor fields describe the
// comment's position at comment time: `line` is the API's line estimate
// (-1 when the API reports none) and `original_line` is the position in
// the original diff (-1 when absent). Neither is claimed to be the current
// revision's line number; `resolution` is "anchored" when a line anchor
// exists and "unresolved" otherwise. Bodies are stored verbatim.
struct ReviewThread {
  long long id{0};
  std::string path{};
  int line{-1};
  int original_line{-1};
  std::string commit_id{};
  std::string original_commit_id{};
  std::string diff_hunk{};
  std::string body{};
  std::string author{};
  std::string url{};
  std::string resolution{};
};

// A page-bounded review-thread listing. `truncated` is true when further
// pages remained after the page cap, so callers must say so instead of
// presenting a partial list as complete.
struct ReviewThreadList {
  std::vector<ReviewThread> threads{};
  bool truncated{false};
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
  // Pure payload parser, directly unit-testable without network.
  static std::vector<GitHubIssue>
  parse_issues(const std::string &payload);
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
  // Inline review threads only (pull-request review comments). Never
  // mixes in issue or conversation comments; those stay on their own
  // endpoints. Throws std::runtime_error when the API call fails, so
  // callers must distinguish the throw (API failure) from an empty
  // vector (no threads). Read-only: performs GET requests only.
  static ReviewThreadList
  get_review_threads(const std::string &owner, const std::string &repo,
                     int pr_number);
  // Pagination core over an injectable fetch, so tests can drive multi-page,
  // failure, and malformed sequences without credentials or network.
  static ReviewThreadList collect_review_threads(
      const std::function<WebResponse(const std::string &)> &fetch,
      const std::string &first_url);
  // Pure helpers, directly unit-testable without credentials or network.
  // The payload is raw JSON text so this header stays free of parser
  // includes; malformed payloads throw std::runtime_error.
  static std::vector<ReviewThread>
  parse_review_threads(const std::string &payload);
  static std::string parse_link_next(const HeaderMap &headers);
  static bool is_available();
};

} // namespace Services
