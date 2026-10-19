#include "github_service.h"
#include "web_service.h"
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace Services {

std::string GitHubService::get_github_token() {
  const char *token = std::getenv("GITHUB_TOKEN");
  if (token != nullptr) {
    return std::string(token);
  }
  return "";
}

std::string GitHubService::get_api_base_url() {
  return "https://api.github.com";
}

WebResponse GitHubService::make_github_request(const std::string &endpoint,
                                               const std::string &method,
                                               const std::string &body) {
  std::string url = get_api_base_url() + endpoint;
  HeaderMap headers;

  std::string token = get_github_token();
  if (!token.empty()) {
    headers["Authorization"] = "Bearer " + token;
  }
  headers["Accept"] = "application/vnd.github.v3+json";
  headers["User-Agent"] = "agentware-agent/1.0";

  if (!body.empty() || method == "POST" || method == "PATCH" ||
      method == "PUT") {
    headers["Content-Type"] = "application/json";
  }

  if (method == "POST") {
    return WebService::post_json(url, body, headers);
  }
  return WebService::fetch_with_headers(url, headers);
}

std::string GitHubService::get_repo_info(const std::string &owner,
                                         const std::string &repo) {
  std::string endpoint = "/repos/" + owner + "/" + repo;
  WebResponse response = make_github_request(endpoint);

  if (response.success) {
    try {
      nlohmann::json json = nlohmann::json::parse(response.content);
      std::stringstream ss;
      ss << "Repository: " << json["full_name"] << "\n";
      ss << "Description: " << json.value("description", "No description")
         << "\n";
      ss << "Stars: " << json.value("stargazers_count", 0) << "\n";
      ss << "Forks: " << json.value("forks_count", 0) << "\n";
      ss << "Language: " << json.value("language", "Unknown") << "\n";
      return ss.str();
    } catch (const std::exception &e) {
      return "Error parsing repository info: " + std::string(e.what());
    }
  } else {
    return "Failed to fetch repository info: " + response.error_message;
  }
}

std::vector<GitHubIssue> GitHubService::get_issues(const std::string &owner,
                                                   const std::string &repo,
                                                   const std::string &state) {
  std::string endpoint =
      "/repos/" + owner + "/" + repo + "/issues?state=" + state;

  WebResponse response = make_github_request(endpoint, "GET", "");

  if (!response.success) {
    throw std::runtime_error("Failed to fetch issues: " +
                             response.error_message);
  }

  try {
    return parse_issues(response.content);
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing issues: " + std::string(e.what()));
  }
}

std::vector<GitHubIssue>
GitHubService::parse_issues(const std::string &payload) {
  std::vector<GitHubIssue> issues;
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(payload);
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing issues: " + std::string(e.what()));
  }
  if (!json.is_array()) {
    throw std::runtime_error("expected a JSON array of issues");
  }
  try {
    for (const auto &item : json) {
      GitHubIssue issue;
      issue.number = item["number"];
      auto text_or_empty = [](const nlohmann::json &o, const char *key) {
        auto it = o.find(key);
        if (it == o.end() || it->is_null()) {
          return std::string{};
        }
        return it->get<std::string>();
      };
      issue.title = text_or_empty(item, "title");
      issue.body = text_or_empty(item, "body");
      issue.state = text_or_empty(item, "state");

      if (item.contains("labels") && item["labels"].is_array()) {
        for (const auto &label : item["labels"]) {
          issue.labels.push_back(label.value("name", ""));
        }
      }

      if (item.contains("assignee") && !item["assignee"].is_null() &&
          item["assignee"].is_object()) {
        issue.assignees.push_back(item["assignee"].value("login", ""));
      }

      if (item.contains("milestone") && !item["milestone"].is_null() &&
          item["milestone"].is_object()) {
        issue.milestone = item["milestone"].value("number", 0);
      }

      issues.push_back(issue);
    }
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing issues: " + std::string(e.what()));
  }

  return issues;
}

GitHubIssue
GitHubService::create_issue(const std::string &owner, const std::string &repo,
                            const std::string &title, const std::string &body,
                            const std::vector<std::string> &labels) {
  std::string endpoint = "/repos/" + owner + "/" + repo + "/issues";
  nlohmann::json payload = {
      {"title", title}, {"body", body}, {"labels", labels}};
  std::string json_body = payload.dump();

  WebResponse response = make_github_request(endpoint, "POST", json_body);

  if (!response.success) {
    throw std::runtime_error("Failed to create issue: " +
                             response.error_message);
  }

  try {
    nlohmann::json json = nlohmann::json::parse(response.content);
    GitHubIssue issue;
    issue.number = json["number"];
    issue.title = json["title"];
    issue.body = json.value("body", "");
    issue.state = json["state"];
    for (const auto &label : json["labels"]) {
      issue.labels.push_back(label["name"]);
    }
    return issue;
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing created issue: " +
                             std::string(e.what()));
  }
}

bool GitHubService::update_issue(
    const std::string &owner, const std::string &repo, int issue_number,
    const std::map<std::string, std::string> &updates) {
  std::string endpoint = "/repos/" + owner + "/" + repo + "/issues/" +
                         std::to_string(issue_number);
  nlohmann::json payload;
  for (const auto &update : updates) {
    payload[update.first] = update.second;
  }
  std::string json_body = payload.dump();

  WebResponse response = make_github_request(endpoint, "PATCH", json_body);

  return response.success;
}

bool GitHubService::add_comment(const std::string &owner,
                                const std::string &repo, int issue_number,
                                const std::string &comment) {
  std::string endpoint = "/repos/" + owner + "/" + repo + "/issues/" +
                         std::to_string(issue_number) + "/comments";
  nlohmann::json payload = {{"body", comment}};
  std::string json_body = payload.dump();

  WebResponse response = make_github_request(endpoint, "POST", json_body);

  return response.success;
}

std::vector<GitHubPR>
GitHubService::get_pull_requests(const std::string &owner,
                                 const std::string &repo,
                                 const std::string &state) {
  std::vector<GitHubPR> prs;
  std::string endpoint =
      "/repos/" + owner + "/" + repo + "/pulls?state=" + state;

  WebResponse response = make_github_request(endpoint, "GET", "");

  if (!response.success) {
    throw std::runtime_error("Failed to fetch PRs: " + response.error_message);
  }

  try {
    nlohmann::json json = nlohmann::json::parse(response.content);
    for (const auto &item : json) {
      GitHubPR pr;
      pr.number = item["number"];
      pr.title = item["title"];
      pr.body = item.value("body", "");
      pr.state = item["state"];
      pr.head_branch = item["head"]["ref"];
      pr.base_branch = item["base"]["ref"];
      pr.mergeable = item.value("mergeable", false);
      prs.push_back(pr);
    }
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing PRs: " + std::string(e.what()));
  }

  return prs;
}

GitHubPR GitHubService::get_pull_request(const std::string &owner,
                                         const std::string &repo,
                                         int pr_number) {
  GitHubPR pr;
  std::string endpoint =
      "/repos/" + owner + "/" + repo + "/pulls/" + std::to_string(pr_number);

  WebResponse response = make_github_request(endpoint, "GET", "");

  if (!response.success) {
    throw std::runtime_error("Failed to fetch PR: " + response.error_message);
  }

  try {
    nlohmann::json json = nlohmann::json::parse(response.content);
    pr.number = json["number"];
    pr.title = json["title"];
    pr.body = json.value("body", "");
    pr.state = json["state"];
    pr.head_branch = json["head"]["ref"];
    pr.base_branch = json["base"]["ref"];
    pr.mergeable = json.value("mergeable", false);
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing PR: " + std::string(e.what()));
  }

  return pr;
}

bool GitHubService::create_pr_comment(const std::string &owner,
                                      const std::string &repo, int pr_number,
                                      const std::string &comment) {
  std::string endpoint = "/repos/" + owner + "/" + repo + "/issues/" +
                         std::to_string(pr_number) + "/comments";
  nlohmann::json payload = {{"body", comment}};
  std::string json_body = payload.dump();

  WebResponse response = make_github_request(endpoint, "POST", json_body);

  return response.success;
}

std::vector<std::string>
GitHubService::get_milestones(const std::string &owner,
                              const std::string &repo) {
  std::vector<std::string> milestones;
  std::string endpoint = "/repos/" + owner + "/" + repo + "/milestones";

  WebResponse response = make_github_request(endpoint, "GET", "");

  if (!response.success) {
    throw std::runtime_error("Failed to fetch milestones: " +
                             response.error_message);
  }

  try {
    nlohmann::json json = nlohmann::json::parse(response.content);
    for (const auto &item : json) {
      milestones.push_back(item["title"]);
    }
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing milestones: " +
                             std::string(e.what()));
  }

  return milestones;
}

int GitHubService::create_milestone(const std::string &owner,
                                    const std::string &repo,
                                    const std::string &title,
                                    const std::string &description) {
  std::string endpoint = "/repos/" + owner + "/" + repo + "/milestones";
  nlohmann::json payload = {{"title", title}, {"description", description}};
  std::string json_body = payload.dump();

  WebResponse response = make_github_request(endpoint, "POST", json_body);

  if (!response.success) {
    return -1;
  }

  try {
    nlohmann::json json = nlohmann::json::parse(response.content);
    return json["number"];
  } catch (const std::exception &) {
    return -1;
  }
}

std::vector<std::string> GitHubService::get_labels(const std::string &owner,
                                                   const std::string &repo) {
  std::vector<std::string> labels;
  std::string endpoint = "/repos/" + owner + "/" + repo + "/labels";

  WebResponse response = make_github_request(endpoint, "GET", "");

  if (!response.success) {
    throw std::runtime_error("Failed to fetch labels: " +
                             response.error_message);
  }

  try {
    nlohmann::json json = nlohmann::json::parse(response.content);
    for (const auto &item : json) {
      labels.push_back(item["name"]);
    }
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing labels: " + std::string(e.what()));
  }

  return labels;
}

bool GitHubService::create_label(const std::string &owner,
                                 const std::string &repo,
                                 const std::string &name,
                                 const std::string &color,
                                 const std::string &description) {
  std::string endpoint = "/repos/" + owner + "/" + repo + "/labels";
  nlohmann::json payload = {
      {"name", name}, {"color", color}, {"description", description}};
  std::string json_body = payload.dump();

  WebResponse response = make_github_request(endpoint, "POST", json_body);

  return response.success;
}

bool GitHubService::create_webhook(const std::string &owner,
                                   const std::string &repo,
                                   const std::string &url,
                                   const std::vector<std::string> &events) {
  std::string endpoint = "/repos/" + owner + "/" + repo + "/hooks";
  nlohmann::json payload = {
      {"name", "web"},
      {"active", true},
      {"events", events},
      {"config", {{"url", url}, {"content_type", "json"}}}};
  std::string json_body = payload.dump();

  WebResponse response = make_github_request(endpoint, "POST", json_body);

  return response.success;
}

std::string GitHubService::run_health_check(const std::string &owner,
                                            const std::string &repo) {
  std::stringstream ss;
  ss << "# Health Check Report for " << owner << "/" << repo << "\n\n";

  ss << "## Repository Information\n";
  ss << get_repo_info(owner, repo) << "\n";

  auto issues = get_issues(owner, repo, "open");
  ss << "## Issues\n";
  ss << "Open issues: " << issues.size() << "\n\n";

  auto prs = get_pull_requests(owner, repo, "open");
  ss << "## Pull Requests\n";
  ss << "Open PRs: " << prs.size() << "\n\n";

  auto milestones = get_milestones(owner, repo);
  ss << "## Milestones\n";
  ss << "Active milestones: " << milestones.size() << "\n";
  for (const auto &milestone : milestones) {
    ss << "- " << milestone << "\n";
  }

  return ss.str();
}

std::string GitHubService::find_related_issues(const std::string &owner,
                                               const std::string &repo,
                                               const std::string &text) {
  auto issues = get_issues(owner, repo, "all");
  std::stringstream ss;
  ss << "Related issues found:\n";

  std::regex word_regex("\\b\\w+\\b");
  std::vector<std::string> keywords;
  auto words_begin = std::sregex_iterator(text.begin(), text.end(), word_regex);
  auto words_end = std::sregex_iterator();

  for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
    std::string word = (*i).str();
    if (word.length() > 3) {
      keywords.push_back(word);
    }
  }

  int found_count = 0;
  for (const auto &issue : issues) {
    bool related = false;
    for (const auto &keyword : keywords) {
      if (issue.title.find(keyword) != std::string::npos ||
          issue.body.find(keyword) != std::string::npos) {
        related = true;
        break;
      }
    }

    if (related) {
      ss << "- #" << issue.number << ": " << issue.title << "\n";
      found_count++;
      if (found_count >= 5)
        break;
    }
  }

  if (found_count == 0) {
    ss << "No related issues found.";
  }

  return ss.str();
}

bool GitHubService::is_available() {
  // GitHub availability depends only on its own credential. It must not
  // depend on WebService::is_available(), which tracks the unrelated
  // search key.
  return !get_github_token().empty();
}

std::vector<ReviewThread>
GitHubService::parse_review_threads(const std::string &payload) {
  std::vector<ReviewThread> threads;
  try {
    nlohmann::json items = nlohmann::json::parse(payload);
    if (!items.is_array()) {
      throw std::runtime_error("expected a JSON array of review comments");
    }
    for (const auto &item : items) {
      ReviewThread thread;
      thread.id = item.value("id", 0LL);
      thread.path = item.value("path", "");
      // The API reports absent positions as explicit nulls; value() only
      // defaults missing keys, so resolve nulls here before converting.
      auto optional_line = [](const nlohmann::json &o, const char *key) {
        auto it = o.find(key);
        if (it == o.end() || it->is_null()) {
          return -1;
        }
        return it->get<int>();
      };
      thread.line = optional_line(item, "line");
      thread.original_line = optional_line(item, "original_line");
      thread.commit_id = item.value("commit_id", "");
      thread.original_commit_id = item.value("original_commit_id", "");
      thread.diff_hunk = item.value("diff_hunk", "");
      // Stored verbatim: bodies are untrusted third-party text and must
      // never be interpreted, only displayed.
      thread.body = item.value("body", "");
      if (item.contains("user") && item["user"].is_object()) {
        thread.author = item["user"].value("login", "");
      }
      thread.url = item.value("html_url", "");
      thread.resolution = (thread.line >= 0) ? "anchored" : "unresolved";
      threads.push_back(thread);
    }
  } catch (const std::exception &e) {
    throw std::runtime_error("Error parsing review threads: " +
                             std::string(e.what()));
  }
  return threads;
}

std::string GitHubService::parse_link_next(const HeaderMap &headers) {
  auto it = headers.find("link");
  if (it == headers.end()) {
    return "";
  }
  // Link: <url>; rel="next", <url>; rel="last", ...
  std::string rest = it->second;
  size_t pos = 0;
  while (pos < rest.size()) {
    size_t open = rest.find('<', pos);
    if (open == std::string::npos) {
      break;
    }
    size_t close = rest.find('>', open + 1);
    if (close == std::string::npos) {
      break;
    }
    std::string url = rest.substr(open + 1, close - open - 1);
    size_t comma = rest.find(',', close + 1);
    std::string params =
        rest.substr(close + 1, comma == std::string::npos ? std::string::npos
                                                          : comma - close - 1);
    if (params.find("rel=\"next\"") != std::string::npos) {
      return url;
    }
    if (comma == std::string::npos) {
      break;
    }
    pos = comma + 1;
  }
  return "";
}

ReviewThreadList GitHubService::collect_review_threads(
    const std::function<WebResponse(const std::string &)> &fetch,
    const std::string &first_url) {
  static const int kMaxPages = 5;
  ReviewThreadList list;
  std::string url = first_url;

  for (int page = 0; page < kMaxPages; ++page) {
    WebResponse response = fetch(url);
    if (!response.success) {
      throw std::runtime_error("Failed to fetch review threads: " +
                               response.error_message);
    }
    std::vector<ReviewThread> page_threads =
        parse_review_threads(response.content);
    list.threads.insert(list.threads.end(), page_threads.begin(),
                        page_threads.end());

    std::string next = parse_link_next(response.headers);
    if (next.empty()) {
      return list;
    }
    url = next;
  }
  // Page cap reached with another page remaining: report truncation so a
  // partial list is never presented as complete.
  list.truncated = true;
  return list;
}

ReviewThreadList GitHubService::get_review_threads(const std::string &owner,
                                                   const std::string &repo,
                                                   int pr_number) {
  std::string first_url = get_api_base_url() + "/repos/" + owner + "/" + repo +
                          "/pulls/" + std::to_string(pr_number) +
                          "/comments?per_page=100";
  auto fetch = [](const std::string &url) {
    HeaderMap headers;
    std::string token = get_github_token();
    if (!token.empty()) {
      headers["Authorization"] = "Bearer " + token;
    }
    headers["Accept"] = "application/vnd.github.v3+json";
    headers["User-Agent"] = "agentware-agent/1.0";
    return WebService::fetch_with_headers(url, headers);
  };
  return collect_review_threads(fetch, first_url);
}

} // namespace Services
