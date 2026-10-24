#pragma once
#include "agent_mode.h"
#include "config.h" // For AGENTWARE_API
#include "search_service.h"
#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Forward declarations for our own types

namespace Data {
class MemoryManager;
}
namespace Services {
class AIService;
}

namespace Core {
class AGENTWARE_API Agent {
public:
  using Mode = AgentMode; // Type alias for backward compatibility

private:
  Mode mode_{Mode::MODE_UNSET};
  std::string api_key_;
  // Using raw pointers with PIMPL idiom would be better for ABI stability
  std::unique_ptr<Data::MemoryManager> memory_;
  std::unique_ptr<Services::AIService> ai_service_;
  // Tool capabilities detected once at startup; see detect_search_capabilities.
  Services::SearchCapabilities search_caps_;
  int command_count_{0};
  // Last result produced by handle_direct_command; read by the model tool-call
  // loop (PR-3) when running in silent mode.
  std::string last_command_result_;

public:
  // Move operations
  Agent(Agent &&) noexcept = default;
  Agent &operator=(Agent &&) noexcept = default;

  // Disable copying
  Agent(const Agent &) = delete;
  Agent &operator=(const Agent &) = delete;

  void initialize_mode();
  int get_user_choice(const std::string &prompt,
                      const std::vector<int> &valid_choices,
                      int default_choice);
  void process_user_input(const std::string &input);
  // out_result, when non-null, receives the tool result instead of
  // last_command_result_. Used by the model tool-call loop to avoid
  // data races when a tool times out and its thread is detached.
  void handle_direct_command(const std::string &input, bool silent = false,
                             std::string *out_result = nullptr,
                             bool record = true);
  void handle_ai_chat(const std::string &input);
  // PR-3 tool-call seam helpers (flag-gated, default off).
  // Extracts a single JSON tool call from assistant text. Returns
  // "none" if no call is present, "error:..." on malformed input,
  // or "" on success with out_tool/out_args populated.
  static std::string extract_tool_call(const std::string &text,
                                       std::string &out_tool,
                                       nlohmann::json &out_args);
  // Truncates an observation to the configured limit with a marker.
  static std::string truncate_observation(const std::string &obs);
  // Wraps an observation as quoted, untrusted data for the model.
  static std::string quote_observation(const std::string &obs);
  // Model-call surface path guard: cwd-only on canonical paths plus
  // refusal of secrets paths. Returns empty string when allowed.
  static std::string model_path_check(const std::string &path);
  // Heuristic: true if the path looks like a secrets file.
  static bool is_secrets_path(const std::string &path);
  void handle_file_injection_command(const std::string &input);
  void handle_shell_command(const std::string &input);
  void handle_meta_command(const std::string &input);

  // Routing rules live in command_router.h so they can be unit tested
  // without agent.h, which the test suite deliberately avoids including.

  // File injection helpers
  std::string process_file_injections(const std::string &input);
  std::string read_file_or_directory(const std::string &path);

  // Helper methods
  bool should_skip_file(const std::string &file_path, const std::string &ext);
  void show_meta_help();
  void clear_screen();
  void handle_chat_management(const std::string &command);
  void show_available_tools();
  void show_memory_context();
  void add_to_memory(const std::string &text);
  void compress_context();
  void show_session_stats();
  void handle_context_management(const std::string &command);
  void handle_multi_file_command(const std::string &command);
  void handle_web_fetch_command(const std::string &command);
  void handle_checkpoint_command(const std::string &command);
  void handle_mcp_command(const std::string &command);
  void handle_theme_command(const std::string &command);
  void handle_auth_command(const std::string &command);
  void handle_sandbox_command(const std::string &command);
  void handle_error_command(const std::string &command);

  [[nodiscard]] bool is_online_mode() const;

  Agent();
  ~Agent();

  void run();
};

// Records one interaction in persistent memory without letting a persistence
// failure terminate the session. Catches only std::runtime_error from the
// persistence layer, warns explicitly, and continues. A failed write is never
// reported as saved. Declared here (rather than hidden in agent.cpp) so tests
// can exercise the boundary directly; see MemoryPersistenceBoundary tests.
void record_interaction(Data::MemoryManager &memory, const std::string &tag,
                        const std::string &result);

// Same boundary for global facts. Note: MemoryManager::save_global_fact
// silently skips unwritable paths instead of throwing, so this helper guards
// the throwing cases; the silent-drop behavior is unchanged and documented in
// the tests.
void record_global_fact(Data::MemoryManager &memory, const std::string &fact);
} // namespace Core
