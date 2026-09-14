#pragma once
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Services {

struct MCPResource {
  std::string uri{};
  std::string name{};
  std::string description{};
  std::string mime_type{};
  nlohmann::json metadata{};
};

struct MCPTool {
  std::string name{};
  std::string description{};
  nlohmann::json input_schema{};
};

struct MCPPrompt {
  std::string name{};
  std::string description{};
  std::vector<std::string> arguments{};
};

struct MCPServer {
  std::string name{};
  std::string executable{};
  std::vector<std::string> args{};
  std::string working_directory{};
  std::map<std::string, std::string> env_vars{};
  bool is_running = false;
  int process_id = -1;
};

class MCPService {
private:
  static std::map<std::string, MCPServer> servers;
  static std::string get_mcp_config_path();
  static void ensure_mcp_directory();
  static nlohmann::json send_mcp_request(const std::string &server_name,
                                         const nlohmann::json &request);
  static bool start_mcp_server(const std::string &server_name);
  static bool stop_mcp_server(const std::string &server_name);
  static nlohmann::json
  create_mcp_request(const std::string &method,
                     const nlohmann::json &params = nlohmann::json::object());

public:
  // Server management
  static bool register_mcp_server(const MCPServer &server);
  static bool unregister_mcp_server(const std::string &server_name);
  static std::vector<std::string> list_mcp_servers();
  static MCPServer get_mcp_server(const std::string &server_name);
  static bool is_server_running(const std::string &server_name);

  // MCP Protocol operations
  static std::vector<MCPResource>
  list_resources(const std::string &server_name);
  static std::string read_resource(const std::string &server_name,
                                   const std::string &uri);
  static std::vector<MCPTool> list_tools(const std::string &server_name);
  static nlohmann::json call_tool(const std::string &server_name,
                                  const std::string &tool_name,
                                  const nlohmann::json &arguments);
  static std::vector<MCPPrompt> list_prompts(const std::string &server_name);
  static std::string get_prompt(const std::string &server_name,
                                const std::string &prompt_name,
                                const nlohmann::json &arguments);

  // Configuration management
  static bool load_server_config();
  static bool save_server_config();
  static void add_default_servers();

  // Utility functions
  static bool ping_server(const std::string &server_name);
  static std::string get_server_status(const std::string &server_name);
  static void cleanup_dead_servers();
};
} // namespace Services
