#pragma once
#include <nlohmann/json.hpp>
#include <string>

// Forward declarations
namespace Core {
enum class AgentMode : std::uint8_t;
}

namespace Services {
class AIService {
private:
  Core::AgentMode mode_;
  std::string api_key_;

  [[nodiscard]] bool is_online_mode() const;
  nlohmann::json create_standard_payload(const std::string &model,
                                         const std::string &user_input,
                                         const std::string &context);
  nlohmann::json create_payload(const std::string &user_input,
                                const std::string &context);
  std::string get_api_url();
  std::string parse_cerebras_stream(const std::string &response);

public:
  AIService(Core::AgentMode mode, const std::string &api_key = "");

  std::string chat(const std::string &user_input, const std::string &context);
  bool is_available();
};
} // namespace Services
