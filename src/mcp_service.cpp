#include "mcp_service.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Services {

std::map<std::string, MCPServer> MCPService::servers;

std::string MCPService::get_mcp_config_path() {
  return "data/mcp_servers.json";
}

void MCPService::ensure_mcp_directory() {
  std::filesystem::create_directories("data");
}

nlohmann::json MCPService::create_mcp_request(const std::string &method,
                                              const nlohmann::json &params) {
  static int request_id = 1;
  nlohmann::json request;
  request["jsonrpc"] = "2.0";
  request["id"] = request_id++;
  request["method"] = method;
  request["params"] = params;
  return request;
}

nlohmann::json MCPService::send_mcp_request(const std::string &server_name,
                                            const nlohmann::json &request) {
  // For now, implement a basic JSON-RPC over stdio communication
  // In a full implementation, this would handle the actual IPC with the MCP
  // server

  auto it = servers.find(server_name);
  if (it == servers.end() || !it->second.is_running) {
    nlohmann::json error_response;
    error_response["error"] = "Server not found or not running";
    return error_response;
  }

  // Simulate MCP communication - in real implementation this would:
  // 1. Send JSON-RPC request to server's stdin
  // 2. Read JSON-RPC response from server's stdout
  // 3. Handle errors and timeouts

  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["id"] = request["id"];

  // Mock responses for demonstration
  std::string method = request["method"];
  if (method == "resources/list") {
    response["result"] = nlohmann::json::array();
  } else if (method == "tools/list") {
    response["result"] = nlohmann::json::array();
  } else if (method == "prompts/list") {
    response["result"] = nlohmann::json::array();
  } else if (method == "ping") {
    response["result"] = "pong";
  } else {
    response["error"] =
        nlohmann::json{{"code", -32601}, {"message", "Method not found"}};
  }

  return response;
}

bool MCPService::start_mcp_server(const std::string &server_name) {
  auto it = servers.find(server_name);
  if (it == servers.end()) {
    return false;
  }

  MCPServer &server = it->second;
  if (server.is_running) {
    return true; // Already running
  }

  try {
    // In a full implementation, this would:
    // 1. Fork a new process
    // 2. Execute the MCP server with proper stdio redirection
    // 3. Set up JSON-RPC communication channels
    // 4. Handle process management

    // For now, simulate server startup
    server.is_running = true;
#ifdef _WIN32
    server.process_id = _getpid(); // Use _getpid() on Windows
#else
    server.process_id = getpid(); // Use getpid() on Unix-like systems
#endif

    std::cout << "Started MCP server: " << server_name << std::endl;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to start MCP server " << server_name << ": "
              << e.what() << std::endl;
    return false;
  }
}

bool MCPService::stop_mcp_server(const std::string &server_name) {
  auto it = servers.find(server_name);
  if (it == servers.end() || !it->second.is_running) {
    return false;
  }

  MCPServer &server = it->second;

  try {
    // In a full implementation, this would:
    // 1. Send shutdown signal to the process
    // 2. Wait for graceful shutdown
    // 3. Force kill if necessary
    // 4. Clean up resources

    server.is_running = false;
    server.process_id = -1;

    std::cout << "Stopped MCP server: " << server_name << std::endl;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to stop MCP server " << server_name << ": " << e.what()
              << std::endl;
    return false;
  }
}

bool MCPService::register_mcp_server(const MCPServer &server) {
  servers[server.name] = server;
  save_server_config();
  return true;
}

bool MCPService::unregister_mcp_server(const std::string &server_name) {
  auto it = servers.find(server_name);
  if (it == servers.end()) {
    return false;
  }

  if (it->second.is_running) {
    stop_mcp_server(server_name);
  }

  servers.erase(it);
  save_server_config();
  return true;
}

std::vector<std::string> MCPService::list_mcp_servers() {
  std::vector<std::string> server_names;
  for (const auto &pair : servers) {
    server_names.push_back(pair.first);
  }
  return server_names;
}

MCPServer MCPService::get_mcp_server(const std::string &server_name) {
  auto it = servers.find(server_name);
  if (it != servers.end()) {
    return it->second;
  }
  return MCPServer{}; // Return empty server if not found
}

bool MCPService::is_server_running(const std::string &server_name) {
  auto it = servers.find(server_name);
  return (it != servers.end() && it->second.is_running);
}

std::vector<MCPResource>
MCPService::list_resources(const std::string &server_name) {
  std::vector<MCPResource> resources;

  if (!is_server_running(server_name)) {
    if (!start_mcp_server(server_name)) {
      return resources;
    }
  }

  nlohmann::json request = create_mcp_request("resources/list");
  nlohmann::json response = send_mcp_request(server_name, request);

  if (response.contains("result") && response["result"].is_array()) {
    for (const auto &resource_json : response["result"]) {
      MCPResource resource;
      resource.uri = resource_json.value("uri", "");
      resource.name = resource_json.value("name", "");
      resource.description = resource_json.value("description", "");
      resource.mime_type = resource_json.value("mimeType", "");
      if (resource_json.contains("metadata")) {
        resource.metadata = resource_json["metadata"];
      }
      resources.push_back(resource);
    }
  }

  return resources;
}

std::string MCPService::read_resource(const std::string &server_name,
                                      const std::string &uri) {
  if (!is_server_running(server_name)) {
    if (!start_mcp_server(server_name)) {
      return "Error: Server not available";
    }
  }

  nlohmann::json params;
  params["uri"] = uri;

  nlohmann::json request = create_mcp_request("resources/read", params);
  nlohmann::json response = send_mcp_request(server_name, request);

  if (response.contains("result") && response["result"].contains("contents")) {
    auto contents = response["result"]["contents"];
    if (contents.is_array() && !contents.empty()) {
      return contents[0].value("text", "");
    }
  }

  return "Error: Failed to read resource";
}

std::vector<MCPTool> MCPService::list_tools(const std::string &server_name) {
  std::vector<MCPTool> tools;

  if (!is_server_running(server_name)) {
    if (!start_mcp_server(server_name)) {
      return tools;
    }
  }

  nlohmann::json request = create_mcp_request("tools/list");
  nlohmann::json response = send_mcp_request(server_name, request);

  if (response.contains("result") && response["result"].contains("tools") &&
      response["result"]["tools"].is_array()) {
    for (const auto &tool_json : response["result"]["tools"]) {
      MCPTool tool;
      tool.name = tool_json.value("name", "");
      tool.description = tool_json.value("description", "");
      if (tool_json.contains("inputSchema")) {
        tool.input_schema = tool_json["inputSchema"];
      }
      tools.push_back(tool);
    }
  }

  return tools;
}

nlohmann::json MCPService::call_tool(const std::string &server_name,
                                     const std::string &tool_name,
                                     const nlohmann::json &arguments) {
  if (!is_server_running(server_name)) {
    if (!start_mcp_server(server_name)) {
      return nlohmann::json{{"error", "Server not available"}};
    }
  }

  nlohmann::json params;
  params["name"] = tool_name;
  params["arguments"] = arguments;

  nlohmann::json request = create_mcp_request("tools/call", params);
  nlohmann::json response = send_mcp_request(server_name, request);

  if (response.contains("result")) {
    return response["result"];
  }

  return nlohmann::json{{"error", "Tool call failed"}};
}

std::vector<MCPPrompt>
MCPService::list_prompts(const std::string &server_name) {
  std::vector<MCPPrompt> prompts;

  if (!is_server_running(server_name)) {
    if (!start_mcp_server(server_name)) {
      return prompts;
    }
  }

  nlohmann::json request = create_mcp_request("prompts/list");
  nlohmann::json response = send_mcp_request(server_name, request);

  if (response.contains("result") && response["result"].contains("prompts") &&
      response["result"]["prompts"].is_array()) {
    for (const auto &prompt_json : response["result"]["prompts"]) {
      MCPPrompt prompt;
      prompt.name = prompt_json.value("name", "");
      prompt.description = prompt_json.value("description", "");
      if (prompt_json.contains("arguments") &&
          prompt_json["arguments"].is_array()) {
        for (const auto &arg : prompt_json["arguments"]) {
          if (arg.contains("name")) {
            prompt.arguments.push_back(arg["name"]);
          }
        }
      }
      prompts.push_back(prompt);
    }
  }

  return prompts;
}

std::string MCPService::get_prompt(const std::string &server_name,
                                   const std::string &prompt_name,
                                   const nlohmann::json &arguments) {
  if (!is_server_running(server_name)) {
    if (!start_mcp_server(server_name)) {
      return "Error: Server not available";
    }
  }

  nlohmann::json params;
  params["name"] = prompt_name;
  params["arguments"] = arguments;

  nlohmann::json request = create_mcp_request("prompts/get", params);
  nlohmann::json response = send_mcp_request(server_name, request);

  if (response.contains("result") && response["result"].contains("messages")) {
    std::string prompt_text;
    for (const auto &message : response["result"]["messages"]) {
      if (message.contains("content") && message["content"].contains("text")) {
        prompt_text += message["content"]["text"].get<std::string>() + "\n";
      }
    }
    return prompt_text;
  }

  return "Error: Failed to get prompt";
}

bool MCPService::load_server_config() {
  try {
    std::string config_path = get_mcp_config_path();
    if (!std::filesystem::exists(config_path)) {
      add_default_servers();
      return true;
    }

    std::ifstream file(config_path);
    nlohmann::json config;
    file >> config;

    servers.clear();

    if (config.contains("servers") && config["servers"].is_object()) {
      for (const auto &[name, server_json] : config["servers"].items()) {
        MCPServer server;
        server.name = name;
        server.executable = server_json.value("executable", "");
        server.working_directory = server_json.value("working_directory", ".");

        if (server_json.contains("args") && server_json["args"].is_array()) {
          for (const auto &arg : server_json["args"]) {
            server.args.push_back(arg.get<std::string>());
          }
        }

        if (server_json.contains("env") && server_json["env"].is_object()) {
          for (const auto &[key, value] : server_json["env"].items()) {
            server.env_vars[key] = value.get<std::string>();
          }
        }

        servers[name] = server;
      }
    }

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to load MCP server config: " << e.what() << std::endl;
    add_default_servers();
    return false;
  }
}

bool MCPService::save_server_config() {
  try {
    ensure_mcp_directory();

    nlohmann::json config;
    config["servers"] = nlohmann::json::object();

    for (const auto &[name, server] : servers) {
      nlohmann::json server_json;
      server_json["executable"] = server.executable;
      server_json["working_directory"] = server.working_directory;
      server_json["args"] = server.args;
      server_json["env"] = server.env_vars;

      config["servers"][name] = server_json;
    }

    std::ofstream file(get_mcp_config_path());
    file << config.dump(2);

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to save MCP server config: " << e.what() << std::endl;
    return false;
  }
}

void MCPService::add_default_servers() {
  // Add some example MCP servers
  MCPServer filesystem_server;
  filesystem_server.name = "filesystem";
  filesystem_server.executable = "npx";
  filesystem_server.args = {"@modelcontextprotocol/server-filesystem", "."};
  filesystem_server.working_directory = ".";
  servers["filesystem"] = filesystem_server;

  MCPServer git_server;
  git_server.name = "git";
  git_server.executable = "npx";
  git_server.args = {"@modelcontextprotocol/server-git", "."};
  git_server.working_directory = ".";
  servers["git"] = git_server;

  save_server_config();
}

bool MCPService::ping_server(const std::string &server_name) {
  if (!is_server_running(server_name)) {
    return false;
  }

  nlohmann::json request = create_mcp_request("ping");
  nlohmann::json response = send_mcp_request(server_name, request);

  return response.contains("result") && response["result"] == "pong";
}

std::string MCPService::get_server_status(const std::string &server_name) {
  auto it = servers.find(server_name);
  if (it == servers.end()) {
    return "Not registered";
  }

  if (!it->second.is_running) {
    return "Stopped";
  }

  if (ping_server(server_name)) {
    return "Running";
  }
  return "Unresponsive";
}

void MCPService::cleanup_dead_servers() {
  for (auto &[name, server] : servers) {
    if (server.is_running && !ping_server(name)) {
      server.is_running = false;
      server.process_id = -1;
    }
  }
}
} // namespace Services
