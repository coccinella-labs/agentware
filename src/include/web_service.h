#pragma once
#include <algorithm>
#include <map>
#include <string>

// Local case-insensitive comparison function for headers
struct CaseInsensitiveCompare {
  bool operator()(const std::string &lhs,
                  const std::string &rhs) const noexcept {
    return std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
        [](unsigned char ac, unsigned char bc) {
          return std::tolower(ac) < std::tolower(bc);
        });
  }
};

using HeaderMap = std::map<std::string, std::string, CaseInsensitiveCompare>;

namespace Services {
struct WebResponse {
  int status_code;
  std::string content;
  std::string content_type;
  HeaderMap headers;
  bool success;
  std::string error_message;
};

class WebService {
private:
  static std::string get_api_key();
  static std::string extract_text_content(const std::string &html);
  static std::string sanitize_content(const std::string &content);

public:
  // Existing search functionality
  static std::string search(const std::string &query);
  static bool is_available();

  // New web fetch capabilities
  static WebResponse fetch_url(const std::string &url);
  static std::string fetch_text(const std::string &url);
  static std::string fetch_json(const std::string &url);
  static WebResponse fetch_with_headers(const std::string &url,
                                        const HeaderMap &headers);
  static WebResponse post_json(const std::string &url,
                               const std::string &json_body,
                               const HeaderMap &headers);
  static bool is_valid_url(const std::string &url);
};
} // namespace Services
