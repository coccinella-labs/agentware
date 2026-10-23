#include "ai_service.h"
#include "agent_mode.h"
#include "config.h"
#include "tool_registry.h"
#include "startup_trace.h"
#include "web_service.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

// Use the HeaderMap from web_service.h

// Reuse the CaseInsensitiveCompare and HeaderMap from web_service.h

namespace Services {

namespace {

std::string test_endpoint(const char *variable, const char *path) {
  const char *base_url = std::getenv(variable);
  if (base_url == nullptr || *base_url == '\0') {
    throw std::runtime_error("Missing test endpoint configuration: " +
                             std::string(variable));
  }
  return std::string(base_url) + path;
}

bool is_timeout_error(const std::string &message) {
  std::string lower = message;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return lower.find("timed out") != std::string::npos ||
         lower.find("timeout") != std::string::npos;
}

} // namespace

AIService::AIService(Core::AgentMode mode, const std::string &api_key)
    : mode_(mode), api_key_(api_key) {}

bool AIService::is_online_mode() const {
  return mode_ == Core::AgentMode::MODE_TOGETHER ||
         mode_ == Core::AgentMode::MODE_CEREBRAS ||
         mode_ == Core::AgentMode::MODE_FIREWORKS ||
         mode_ == Core::AgentMode::MODE_GROQ ||
         mode_ == Core::AgentMode::MODE_DEEPSEEK ||
         mode_ == Core::AgentMode::MODE_OPENAI;
}

bool AIService::is_available() {
  // For online providers API key is required.
  if (is_online_mode()) {
    return !api_key_.empty();
  }
  // Offline modes don't need an API key (they talk to local server)
  return true;
}

std::string AIService::get_api_url() {
  if (std::getenv("TEST_MODE")) {
    // Docker Compose supplies the isolated mock endpoints for E2E testing.
    switch (mode_) {
    case Core::AgentMode::MODE_TOGETHER:
      return test_endpoint("TEST_TOGETHER_URL", "/v1/chat/completions");
    case Core::AgentMode::MODE_CEREBRAS:
      return test_endpoint("TEST_CEREBRAS_URL", "/v1/chat/completions");
    case Core::AgentMode::MODE_FIREWORKS:
      return test_endpoint("TEST_FIREWORKS_URL",
                           "/inference/v1/chat/completions");
    case Core::AgentMode::MODE_GROQ:
      return test_endpoint("TEST_GROQ_URL", "/openai/v1/chat/completions");
    case Core::AgentMode::MODE_DEEPSEEK:
      return test_endpoint("TEST_DEEPSEEK_URL", "/v1/chat/completions");
    case Core::AgentMode::MODE_OPENAI:
      return test_endpoint("TEST_OPENAI_URL", "/v1/chat/completions");
    case Core::AgentMode::MODE_LLAMA_3B:
    case Core::AgentMode::MODE_LLAMA_LATEST:
    case Core::AgentMode::MODE_LLAMA_31:
    default:
      return test_endpoint("TEST_OLLAMA_URL", "/api/chat");
    }
  } else {
    switch (mode_) {
    case Core::AgentMode::MODE_TOGETHER:
      return "https://api.together.xyz/v1/chat/completions";
    case Core::AgentMode::MODE_CEREBRAS:
      return "https://api.cerebras.ai/v1/chat/completions";
    case Core::AgentMode::MODE_FIREWORKS:
      return "https://api.fireworks.ai/inference/v1/chat/completions";
    case Core::AgentMode::MODE_GROQ:
      return "https://api.groq.com/openai/v1/chat/completions";
    case Core::AgentMode::MODE_DEEPSEEK:
      return "https://api.deepseek.com/v1/chat/completions";
    case Core::AgentMode::MODE_OPENAI:
      return "https://api.openai.com/v1/chat/completions";
    case Core::AgentMode::MODE_LLAMA_3B:
    case Core::AgentMode::MODE_LLAMA_LATEST:
    case Core::AgentMode::MODE_LLAMA_31:
    default:
      return "http://localhost:11434/api/chat";
    }
  }
}

nlohmann::json AIService::create_standard_payload(const std::string &model,
                                                  const std::string &user_input,
                                                  const std::string &context) {
  std::string system_prompt = "You are a helpful AI assistant. " + context;
  return {{"model", model},
          {"messages",
           {{{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", user_input}}}},
          {"max_tokens", 1000},
          {"temperature", 0.7}};
}

nlohmann::json AIService::create_payload(const std::string &user_input,
                                         const std::string &context) {
  Core::register_all_tools();
  const std::string loop_setting =
      Utils::Config::get_env_var("AGENTWARE_TOOL_CALL_LOOP", "0");
  const std::string system_prompt = Core::render_model_system_prompt(
      context, loop_setting == "1" || loop_setting == "true");

  switch (mode_) {
  case Core::AgentMode::MODE_TOGETHER: // Together AI
    return create_standard_payload(
        "meta-llama/Llama-3.3-70B-Instruct-Turbo-Free", user_input,
        system_prompt);
  case Core::AgentMode::MODE_CEREBRAS: // Cerebras
    return {{"model", "llama-4-maverick-17b-128e-instruct"},
            {"messages",
             {{{"role", "system"}, {"content", system_prompt}},
              {{"role", "user"}, {"content", user_input}}}},
            {"stream", true},
            {"max_completion_tokens", 4096},
            {"temperature", 0.7},
            {"top_p", 0.9}};
  case Core::AgentMode::MODE_FIREWORKS: // Fireworks
    return create_standard_payload(
        "accounts/fireworks/models/llama-v3-70b-instruct", user_input,
        system_prompt);
  case Core::AgentMode::MODE_GROQ: // Groq
    return create_standard_payload("llama-3.1-70b-versatile", user_input,
                                   system_prompt);
  case Core::AgentMode::MODE_DEEPSEEK: // DeepSeek
    return create_standard_payload("deepseek-chat", user_input, system_prompt);
  case Core::AgentMode::MODE_OPENAI: // OpenAI
    return create_standard_payload("gpt-4", user_input, system_prompt);
  case Core::AgentMode::MODE_LLAMA_3B: // Llama 3B (local)
    return {{"model", "llama3.2:3b"},
            {"stream", false},
            {"messages",
             {{{"role", "system"}, {"content", system_prompt}},
              {{"role", "user"}, {"content", user_input}}}}};

  case Core::AgentMode::MODE_LLAMA_LATEST: // Llama latest (local)
    return {{"model", "llama3.2:latest"},
            {"stream", false},
            {"messages",
             {{{"role", "system"}, {"content", system_prompt}},
              {{"role", "user"}, {"content", user_input}}}}};

  case Core::AgentMode::MODE_LLAMA_31: // Llama 3.1 (local)
    return {{"model", "llama3.1:latest"},
            {"stream", false},
            {"messages",
             {{{"role", "system"}, {"content", system_prompt}},
              {{"role", "user"}, {"content", user_input}}}}};

  default: // Fallback to Llama 3B
    return {{"model", "llama3.2:3b"},
            {"stream", false},
            {"messages",
             {{{"role", "system"}, {"content", system_prompt}},
              {{"role", "user"}, {"content", user_input}}}}};
  }
}

std::string AIService::parse_cerebras_stream(const std::string &response) {
  // Response is server-sent events style; accumulate content deltas.
  std::string result;
  std::istringstream stream(response);
  std::string line;

  while (std::getline(stream, line)) {
    // trim leading spaces
    if (line.rfind("data: ", 0) == 0) {
      std::string json_str = line.substr(6);
      if (json_str == "[DONE]")
        break;

      try {
        auto json = nlohmann::json::parse(json_str);
        if (json.contains("choices") && !json["choices"].empty()) {
          auto &choice = json["choices"][0];
          if (choice.contains("delta") && choice["delta"].contains("content")) {
            // content might be string
            result += choice["delta"]["content"].get<std::string>();
          } else if (choice.contains("text")) {
            result += choice["text"].get<std::string>();
          }
        }
      } catch (const std::exception &) {
        // ignore malformed chunk and continue
        continue;
      }
    }
  }
  return result;
}

std::string safe_get_string(const nlohmann::json &j,
                            const std::initializer_list<std::string> &path,
                            const std::string &fallback = "") {
  const nlohmann::json *cur = &j;
  for (const auto &p : path) {
    if (!cur->is_object() || !cur->contains(p))
      return fallback;
    cur = &((*cur)[p]);
  }
  if (cur->is_string())
    return cur->get<std::string>();
  return fallback;
}

std::string AIService::chat(const std::string &user_input,
                            const std::string &context,
                            long request_timeout_seconds) {
  if (!is_available()) {
    return "Error: AI service is not available. Please check your API key and "
           "internet connection.";
  }

  Utils::startup_trace("ai: chat entered");
  auto payload = create_payload(user_input, context);
  Utils::startup_trace("ai: payload built");
  auto url = get_api_url();
  Utils::startup_trace("ai: endpoint resolved: " + url);

  try {
    // Set up headers
    HeaderMap headers = {{"Content-Type", "application/json"},
                         {"Accept", "text/event-stream"}};

    // Set API key in appropriate header based on service
    switch (mode_) {
    case Core::AgentMode::MODE_TOGETHER:  // Together AI
    case Core::AgentMode::MODE_FIREWORKS: // Fireworks
    case Core::AgentMode::MODE_GROQ:      // Groq
    case Core::AgentMode::MODE_DEEPSEEK:  // DeepSeek
    case Core::AgentMode::MODE_OPENAI:    // OpenAI
      headers["Authorization"] = "Bearer " + api_key_;
      break;
    case Core::AgentMode::MODE_CEREBRAS: // Cerebras
      headers["X-API-Key"] = api_key_;
      break;
    case Core::AgentMode::MODE_LLAMA_3B: // Local Ollama
    case Core::AgentMode::MODE_LLAMA_LATEST:
    case Core::AgentMode::MODE_LLAMA_31:
      // No API key needed for local
      break;
    case Core::AgentMode::MODE_UNSET:
      // No action
      break;
    }

    // Use WebService to make the HTTP request
    WebService web_service;
    std::string json_body = payload.dump();

    WebResponse response;
    if (mode_ == Core::AgentMode::MODE_TOGETHER ||
        mode_ == Core::AgentMode::MODE_CEREBRAS ||
        mode_ == Core::AgentMode::MODE_FIREWORKS ||
        mode_ == Core::AgentMode::MODE_GROQ ||
        mode_ == Core::AgentMode::MODE_DEEPSEEK ||
        mode_ == Core::AgentMode::MODE_OPENAI ||
        mode_ == Core::AgentMode::MODE_LLAMA_3B ||
        mode_ == Core::AgentMode::MODE_LLAMA_LATEST ||
        mode_ == Core::AgentMode::MODE_LLAMA_31) {
      // All providers use POST
      response = web_service.post_json(url, json_body, headers,
                                       request_timeout_seconds);
    } else {
      // Fallback (should not happen)
      response = web_service.post_json(url, json_body, headers,
                                       request_timeout_seconds);
    }
    Utils::startup_trace("ai: response received, status " +
                         std::to_string(response.status_code));

    if (response.status_code != 200) {
      if (request_timeout_seconds > 0 && response.status_code == 0 &&
          is_timeout_error(response.error_message)) {
        return "Error: model request timeout after " +
               std::to_string(request_timeout_seconds) +
               "s (network timeout): " + response.error_message;
      }
      std::string error_content = response.content;
      if (error_content.length() > 500) {
        error_content = error_content.substr(0, 500) + "...[truncated]";
      }
      return "Error: AI service returned status code " +
             std::to_string(response.status_code) + " - " + error_content +
             " | Error: " + response.error_message;
    }

    // Handle streaming response for Cerebras
    if (mode_ == Core::AgentMode::MODE_CEREBRAS) {
      return parse_cerebras_stream(response.content);
    }

    // Parse the response based on the service
    auto json_response = nlohmann::json::parse(response.content);

    // Handle different response formats
    switch (mode_) {
    case Core::AgentMode::MODE_TOGETHER:  // Together AI
    case Core::AgentMode::MODE_FIREWORKS: // Fireworks
    case Core::AgentMode::MODE_GROQ:      // Groq
    case Core::AgentMode::MODE_DEEPSEEK:  // DeepSeek
    case Core::AgentMode::MODE_OPENAI:    // OpenAI
    case Core::AgentMode::MODE_CEREBRAS:  // Cerebras
      if (json_response.contains("choices") &&
          !json_response["choices"].empty()) {
        auto &choice = json_response["choices"][0];
        if (choice.contains("message") &&
            choice["message"].contains("content")) {
          return choice["message"]["content"].get<std::string>();
        } else if (choice.contains("text")) {
          return choice["text"].get<std::string>();
        }
      }
      break;
    case Core::AgentMode::MODE_LLAMA_3B: // Local Ollama
    case Core::AgentMode::MODE_LLAMA_LATEST:
    case Core::AgentMode::MODE_LLAMA_31:
      if (json_response.contains("message") &&
          json_response["message"].contains("content")) {
        return json_response["message"]["content"].get<std::string>();
      } else if (json_response.contains("response")) {
        return json_response["response"].get<std::string>();
      }
      break;
    case Core::AgentMode::MODE_UNSET:
      // No action
      break;
    }

    // If we get here, the response format wasn't as expected
    return "Error: Unexpected response format from AI service: " +
           response.content;

  } catch (const std::exception &e) {
    return "Error: " + std::string(e.what());
  }
  return "Error: Unknown error occurred in AI service";
}

} // namespace Services
