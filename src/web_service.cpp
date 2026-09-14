#include "web_service.h"
#include "config.h"
#include <algorithm>
#include <cpr/cpr.h>
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

const long kDefaultTimeoutSeconds = 30;
const size_t kMaxContentLength = 8000;

// Callback function for writing response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  size_t real_size = size * nmemb;
  std::string *response = static_cast<std::string *>(userp);
  // Reserve space to avoid frequent reallocations
  if (response->capacity() < response->size() + real_size) {
    response->reserve(response->size() + real_size + 4096);
  }
  response->append(static_cast<char *>(contents), real_size);
  return real_size;
}

// Callback function for writing header data
static size_t HeaderCallback(char *buffer, size_t size, size_t nitems,
                             void *userdata) {
  size_t real_size = size * nitems;
  std::string *headers = static_cast<std::string *>(userdata);
  headers->append(buffer, real_size);
  return real_size;
}

namespace Services {
std::string WebService::get_api_key() {
  return Utils::Config::get_env_var("SERPAPI_KEY");
}

bool WebService::is_available() { return !get_api_key().empty(); }

std::string WebService::search(const std::string &query) {

  try {
    std::string api_key = get_api_key();
    if (api_key.empty()) {
      return "Error: SERPAPI_KEY is missing.";
    }

    // URL encode the query
    std::string encoded_query;
    CURL *curl = curl_easy_init();
    if (curl) {
      char *output = curl_easy_escape(curl, query.c_str(),
                                      static_cast<int>(query.length()));
      if (output) {
        encoded_query = output;
        curl_free(output);
      }
      curl_easy_cleanup(curl);
    } else {
      encoded_query = query; // Fallback to unencoded query if curl init fails
    }

    std::string search_url = "https://api.duckduckgo.com/?q=" + encoded_query +
                             "&api_key=" + api_key;

    std::string response_str;
    std::string response_headers;

    CURL *search_curl = curl_easy_init();
    if (search_curl) {
      curl_easy_setopt(search_curl, CURLOPT_URL, search_url.c_str());
      curl_easy_setopt(search_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(search_curl, CURLOPT_WRITEDATA, &response_str);
      curl_easy_setopt(search_curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
      curl_easy_setopt(search_curl, CURLOPT_HEADERDATA, &response_headers);
      curl_easy_setopt(search_curl, CURLOPT_TIMEOUT, 5L); // 5s timeout
      curl_easy_setopt(search_curl, CURLOPT_USERAGENT, "Agentware-Agent/1.0");

      CURLcode res = curl_easy_perform(search_curl);
      if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res)
                  << std::endl;
        return "Error: Failed to perform web search";
      }

      long http_code = 0;
      curl_easy_getinfo(search_curl, CURLINFO_RESPONSE_CODE, &http_code);
      if (http_code != 200) {
        std::cerr << "HTTP request failed with code: " << http_code
                  << std::endl;
        return "Error: Failed to perform web search (HTTP " +
               std::to_string(http_code) + ")";
      }

      curl_easy_cleanup(search_curl);
    } else {
      return "Error: Failed to initialize cURL";
    }

    // Parse JSON response
    try {
      auto json = nlohmann::json::parse(response_str);
      std::string result;

      // Extract and format the search results
      if (json.contains("AbstractText") && !json["AbstractText"].is_null()) {
        result = json["AbstractText"].get<std::string>();
        if (!result.empty()) {
          return "Abstract: " + result;
        }
      }

      if (json.contains("RelatedTopics") && json["RelatedTopics"].is_array()) {
        for (const auto &topic : json["RelatedTopics"]) {
          if (topic.contains("Text")) {
            result += "• " + topic["Text"].get<std::string>() + "\n";
          }
        }
      }

      return result.empty() ? "No results found." : result;
    } catch (const std::exception &e) {
      return "Error parsing search results: " + std::string(e.what());
    }

  } catch (const std::exception &e) {
    return std::string("Error performing search: ") + e.what();
  }
}

bool WebService::is_valid_url(const std::string &url) {
  // Basic URL validation
  if (url.empty())
    return false;

  // Check for common protocols
  if (url.find("http://") == 0 || url.find("https://") == 0) {
    // Must have at least a domain after protocol
    size_t domain_start = url.find("://") + 3;
    if (domain_start < url.length() &&
        url.find('.', domain_start) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string WebService::extract_text_content(const std::string &html) {
  std::string text = html;

  // Remove script and style tags and their content
  size_t pos = 0;
  while ((pos = text.find("<script", pos)) != std::string::npos) {
    size_t end = text.find("</script>", pos);
    if (end != std::string::npos) {
      text.erase(pos, end - pos + 9);
    } else {
      break;
    }
  }

  pos = 0;
  while ((pos = text.find("<style", pos)) != std::string::npos) {
    size_t end = text.find("</style>", pos);
    if (end != std::string::npos) {
      text.erase(pos, end - pos + 8);
    } else {
      break;
    }
  }

  // Remove HTML tags
  pos = 0;
  while ((pos = text.find('<', pos)) != std::string::npos) {
    size_t end = text.find('>', pos);
    if (end != std::string::npos) {
      text.erase(pos, end - pos + 1);
    } else {
      break;
    }
  }

  return sanitize_content(text);
}

std::string WebService::sanitize_content(const std::string &content) {
  std::string sanitized = content;

  // Replace HTML entities
  size_t pos = 0;
  while ((pos = sanitized.find("&amp;", pos)) != std::string::npos) {
    sanitized.replace(pos, 5, "&");
    pos += 1;
  }
  while ((pos = sanitized.find("&lt;", pos)) != std::string::npos) {
    sanitized.replace(pos, 4, "<");
    pos += 1;
  }
  while ((pos = sanitized.find("&gt;", pos)) != std::string::npos) {
    sanitized.replace(pos, 4, ">");
    pos += 1;
  }
  while ((pos = sanitized.find("&quot;", pos)) != std::string::npos) {
    sanitized.replace(pos, 6, "\"");
    pos += 1;
  }
  while ((pos = sanitized.find("&#39;", pos)) != std::string::npos) {
    sanitized.replace(pos, 5, "'");
    pos += 1;
  }

  // Clean up whitespace
  pos = 0;
  while ((pos = sanitized.find("\n\n\n", pos)) != std::string::npos) {
    sanitized.replace(pos, 3, "\n\n");
  }

  // Trim leading/trailing whitespace
  size_t start = sanitized.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";

  size_t end = sanitized.find_last_not_of(" \t\n\r");
  return sanitized.substr(start, end - start + 1);
}

WebResponse WebService::fetch_url(const std::string &url) {
  WebResponse response;
  response.success = false;

  if (!is_valid_url(url)) {
    response.error_message = "Invalid URL format";
    response.status_code = 0;
    return response;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    response.error_message = "Failed to initialize cURL";
    return response;
  }

  try {
    std::string response_body;
    std::string response_headers;
    long http_code = 0;

    // Set up curl options
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Agentware-Agent/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10s timeout
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);

    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      response.error_message =
          "cURL error: " + std::string(curl_easy_strerror(res));
      curl_easy_cleanup(curl);
      return response;
    }

    // Get HTTP status code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    // Process response
    response.status_code = static_cast<int>(http_code);
    response.content = response_body;
    response.success = (http_code >= 200 && http_code < 300);

    // Parse headers
    std::istringstream header_stream(response_headers);
    std::string header_line;
    while (std::getline(header_stream, header_line, '\n')) {
      size_t colon_pos = header_line.find(':');
      if (colon_pos != std::string::npos) {
        std::string key = header_line.substr(0, colon_pos);
        std::string value = header_line.substr(colon_pos + 1);

        // Trim whitespace
        key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch) {
                    return !std::isspace(ch);
                  }));
        key.erase(std::find_if(key.rbegin(), key.rend(),
                               [](int ch) { return !std::isspace(ch); })
                      .base(),
                  key.end());

        value.erase(value.begin(),
                    std::find_if(value.begin(), value.end(),
                                 [](int ch) { return !std::isspace(ch); }));
        value.erase(std::find_if(value.rbegin(), value.rend(),
                                 [](int ch) { return !std::isspace(ch); })
                        .base(),
                    value.end());

        if (!key.empty()) {
          response.headers[key] = value;
        }
      }
    }

    // Set content type
    auto content_type_it = response.headers.find("content-type");
    if (content_type_it != response.headers.end()) {
      response.content_type = content_type_it->second;
    }

    if (!response.success) {
      response.error_message = "HTTP " + std::to_string(response.status_code);
    }

    curl_easy_cleanup(curl);

  } catch (const std::exception &e) {
    curl_easy_cleanup(curl);
    response.error_message = "Exception: " + std::string(e.what());
    response.success = false;

  } catch (...) {
    curl_easy_cleanup(curl);
    response.error_message = "Unknown exception";
    response.success = false;
  }

  return response;
}

std::string WebService::fetch_text(const std::string &url) {
  WebResponse response = fetch_url(url);

  if (!response.success) {
    return "Error fetching URL: " + response.error_message;
  }

  // Check if content is HTML and extract text
  if (response.content_type.find("text/html") != std::string::npos) {
    std::string extracted = extract_text_content(response.content);
    if (extracted.length() > kMaxContentLength) {
      extracted = extracted.substr(0, kMaxContentLength) +
                  "\n\n[Content truncated - showing first " +
                  std::to_string(kMaxContentLength) + " characters]";
    }
    return extracted;
  }

  // For plain text or other content types
  if (response.content.length() > kMaxContentLength) {
    return response.content.substr(0, kMaxContentLength) +
           "\n\n[Content truncated - showing first " +
           std::to_string(kMaxContentLength) + " characters]";
  }

  return response.content;
}

std::string WebService::fetch_json(const std::string &url) {
  try {
    auto response = fetch_url(url);
    if (response.status_code == 200) {
      // Validate JSON
      auto json = nlohmann::json::parse(response.content);
      // Pretty print JSON with truncation if needed
      std::string formatted = json.dump(2);
      if (formatted.length() > kMaxContentLength) {
        formatted = formatted.substr(0, kMaxContentLength) +
                    "\n\n[JSON truncated - showing first " +
                    std::to_string(kMaxContentLength) + " characters]";
      }
      return formatted;
    } else {
      return "Error: " + std::to_string(response.status_code) + " - " +
             response.error_message;
    }
  } catch (const std::exception &e) {
    return "Error: " + std::string(e.what());
  }
}

WebResponse WebService::fetch_with_headers(const std::string &url,
                                           const HeaderMap &headers) {
  CURL *curl = curl_easy_init();
  WebResponse response;

  if (!curl) {
    response.status_code = 0;
    response.error_message = "Failed to initialize CURL";
    response.success = false;
    return response;
  }

  std::string response_body;
  std::string response_headers;
  struct curl_slist *header_list = nullptr;

  // Add custom headers to the request
  for (const auto &[key, value] : headers) {
    std::string header = key + ": " + value;
    header_list = curl_slist_append(header_list, header.c_str());
  }

  // Set CURL options
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, kDefaultTimeoutSeconds);

  // Perform the request
  CURLcode res = curl_easy_perform(curl);

  // Get the HTTP status code
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  // Get content type if available
  char *content_type = nullptr;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);

  // Clean up
  if (header_list) {
    curl_slist_free_all(header_list);
  }
  curl_easy_cleanup(curl);

  // Set response properties
  response.status_code = static_cast<int>(http_code);
  response.content = response_body;
  response.content_type = content_type ? content_type : "";
  response.success = (res == CURLE_OK);

  if (res != CURLE_OK) {
    response.error_message = curl_easy_strerror(res);
  } else if (http_code >= 400) {
    response.error_message = "HTTP error " + std::to_string(http_code);
  }

  // Parse response headers
  std::istringstream header_stream(response_headers);
  std::string header_line;
  while (std::getline(header_stream, header_line)) {
    size_t colon_pos = header_line.find(':');
    if (colon_pos != std::string::npos) {
      std::string key = header_line.substr(0, colon_pos);
      std::string value = header_line.substr(colon_pos + 1);
      // Trim whitespace
      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);
      response.headers[key] = value;
    }
  }

  return response;
}

WebResponse WebService::post_json(const std::string &url,
                                  const std::string &json_body,
                                  const HeaderMap &headers) {
  WebResponse response;
  response.success = false;

  try {
    // Prepare headers for CPR
    cpr::Header cpr_headers;
    for (const auto &[key, value] : headers) {
      cpr_headers[key] = value;
    }

    // Set Content-Type if not provided
    if (headers.find("Content-Type") == headers.end()) {
      cpr_headers["Content-Type"] = "application/json";
    }

    // Make POST request
    cpr::Response cpr_response =
        cpr::Post(cpr::Url{url}, cpr_headers, cpr::Body{json_body},
                  cpr::Timeout{std::chrono::seconds{kDefaultTimeoutSeconds}});

    // Fill response
    response.status_code = static_cast<int>(cpr_response.status_code);
    response.content = cpr_response.text;
    response.success =
        (cpr_response.status_code >= 200 && cpr_response.status_code < 300);

    if (!response.success) {
      response.error_message = "HTTP " +
                               std::to_string(cpr_response.status_code) +
                               " | Error: " + cpr_response.error.message;
    }

    // Parse headers
    for (const auto &[key, value] : cpr_response.header) {
      response.headers[key] = value;
    }

    // Set content type
    auto content_type_it = response.headers.find("content-type");
    if (content_type_it != response.headers.end()) {
      response.content_type = content_type_it->second;
    }

  } catch (const std::exception &e) {
    response.error_message = "Exception: " + std::string(e.what());
  } catch (...) {
    response.error_message = "Unknown exception";
  }

  return response;
}
} // namespace Services
