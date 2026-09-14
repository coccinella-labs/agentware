#include "sandbox_service.h"
#include "platform.h"
#include "validation.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <nlohmann/json.hpp>

namespace Services {

std::map<std::string, SandboxConfig> SandboxService::sandbox_configs_;

std::string SandboxService::get_sandbox_config_path() {
  return "data/sandbox_config.json";
}

void SandboxService::ensure_sandbox_directory() {
  std::filesystem::create_directories("data");
  std::filesystem::create_directories("data/sandbox");
}

void SandboxService::initialize_default_configs() {
  // Default sandbox for general use
  SandboxConfig default_config;
  default_config.name = "default";
  default_config.image = "ubuntu:22.04";
  default_config.memory_limit_mb = 512;
  default_config.cpu_limit_percent = 50;
  default_config.timeout_seconds = 300;
  default_config.network_access = false;
  default_config.working_directory = "/workspace";
  default_config.allowed_commands = {"ls",   "cat",  "echo", "grep",
                                     "find", "head", "tail", "wc"};
  sandbox_configs_["default"] = default_config;

  // Python sandbox
  SandboxConfig python_config;
  python_config.name = "python";
  python_config.image = "python:3.11-slim";
  python_config.memory_limit_mb = 1024;
  python_config.cpu_limit_percent = 75;
  python_config.timeout_seconds = 600;
  python_config.network_access = false;
  python_config.working_directory = "/workspace";
  python_config.allowed_commands = {"python", "python3", "pip",
                                    "ls",     "cat",     "echo"};
  sandbox_configs_["python"] = python_config;

  // Node.js sandbox
  SandboxConfig node_config;
  node_config.name = "nodejs";
  node_config.image = "node:18-slim";
  node_config.memory_limit_mb = 1024;
  node_config.cpu_limit_percent = 75;
  node_config.timeout_seconds = 600;
  node_config.network_access = false;
  node_config.working_directory = "/workspace";
  node_config.allowed_commands = {"node", "npm", "ls", "cat", "echo"};
  sandbox_configs_["nodejs"] = node_config;

  // Secure shell sandbox
  SandboxConfig shell_config;
  shell_config.name = "shell";
  shell_config.image = "alpine:latest";
  shell_config.memory_limit_mb = 256;
  shell_config.cpu_limit_percent = 25;
  shell_config.timeout_seconds = 120;
  shell_config.network_access = false;
  shell_config.working_directory = "/workspace";
  shell_config.allowed_commands = {"sh",   "ls",   "cat",  "echo",
                                   "grep", "find", "head", "tail"};
  sandbox_configs_["shell"] = shell_config;
}

std::string SandboxService::generate_container_name() {
  static int counter = 1;
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  ss << "agentware_sandbox_" << time_t << "_" << counter++;
  return ss.str();
}

bool SandboxService::is_docker_available() {
  FILE *pipe = popen("docker --version 2>&1", "r");
  if (!pipe)
    return false;

  char buffer[128];
  bool success = false;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    if (strstr(buffer, "Docker version") != nullptr) {
      success = true;
      break;
    }
  }
  pclose(pipe);
  return success;
}

std::string SandboxService::escape_shell_arg(const std::string &arg) {
  std::string escaped = arg;
  // Basic shell escaping - replace single quotes
  size_t pos = 0;
  while ((pos = escaped.find("'", pos)) != std::string::npos) {
    escaped.replace(pos, 1, "'\"'\"'");
    pos += 5;
  }
  return "'" + escaped + "'";
}

void SandboxService::initialize() {
  initialize_default_configs();
  load_sandbox_config();
}

bool SandboxService::create_sandbox_config(const std::string &name,
                                           const SandboxConfig &config) {
  sandbox_configs_[name] = config;
  return save_sandbox_config();
}

bool SandboxService::remove_sandbox_config(const std::string &name) {
  // Don't allow removal of built-in configs
  if (name == "default" || name == "python" || name == "nodejs" ||
      name == "shell") {
    return false;
  }

  auto it = sandbox_configs_.find(name);
  if (it == sandbox_configs_.end()) {
    return false;
  }

  sandbox_configs_.erase(it);
  return save_sandbox_config();
}

std::vector<std::string> SandboxService::list_sandbox_configs() {
  std::vector<std::string> config_names;
  for (const auto &[name, config] : sandbox_configs_) {
    config_names.push_back(name);
  }
  return config_names;
}

SandboxConfig SandboxService::get_sandbox_config(const std::string &name) {
  auto it = sandbox_configs_.find(name);
  if (it != sandbox_configs_.end()) {
    return it->second;
  }
  return SandboxConfig{}; // Return empty config if not found
}

SandboxResult SandboxService::execute_command(const std::string &command,
                                              const std::string &sandbox_name,
                                              const std::string &working_dir) {
  SandboxResult result;

  if (!is_docker_available()) {
    result.error_message = "Docker is not available";
    result.exit_code = -1;
    return result;
  }

  auto it = sandbox_configs_.find(sandbox_name);
  if (it == sandbox_configs_.end()) {
    result.error_message = "Sandbox configuration not found: " + sandbox_name;
    result.exit_code = -1;
    return result;
  }

  const SandboxConfig &config = it->second;

  // Security check
  if (!is_command_safe(command)) {
    result.error_message = "Command failed security validation";
    result.exit_code = -1;
    return result;
  }

  // Build docker command
  std::string container_name = generate_container_name();
  std::stringstream docker_cmd;

  docker_cmd << "docker run --rm";
  docker_cmd << " --name " << container_name;
  docker_cmd << " --memory=" << config.memory_limit_mb << "m";
  docker_cmd << " --cpus=" << (config.cpu_limit_percent / 100.0);

  if (!config.network_access) {
    docker_cmd << " --network=none";
  }

  // Set working directory
  std::string work_dir =
      working_dir.empty() ? config.working_directory : working_dir;
  docker_cmd << " --workdir=" << work_dir;

  // Add environment variables
  for (const auto &[key, value] : config.environment_vars) {
    docker_cmd << " -e " << key << "=" << escape_shell_arg(value);
  }

  // Add volumes (be careful with security)
  for (const auto &volume : config.volumes) {
    docker_cmd << " -v " << volume;
  }

  docker_cmd << " " << config.image;
  docker_cmd << " sh -c " << escape_shell_arg(command);

  // Execute with timeout
  auto start_time = std::chrono::high_resolution_clock::now();

  std::string full_command = docker_cmd.str() + " 2>&1";
  FILE *pipe = Utils::Platform::open_process(full_command, "r");

  if (!pipe) {
    result.error_message = "Failed to execute docker command";
    result.exit_code = -1;
    return result;
  }

  std::stringstream output;
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output << buffer;
  }

  result.exit_code = Utils::Platform::close_process(pipe);
  result.stdout_output = output.str();

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  result.execution_time_seconds = duration.count() / 1000.0;

  // Check for timeout
  if (result.execution_time_seconds > config.timeout_seconds) {
    result.timed_out = true;
    result.error_message = "Command execution timed out";
  }

  return result;
}

SandboxResult SandboxService::execute_script(const std::string &script_content,
                                             const std::string &script_type,
                                             const std::string &sandbox_name) {
  SandboxResult result;

  // Create temporary script file
  std::string script_path = "data/sandbox/temp_script." + script_type;
  std::ofstream script_file(script_path);
  script_file << script_content;
  script_file.close();

  // Execute based on script type
  std::string command;
  if (script_type == "py" || script_type == "python") {
    command = "python " + script_path;
  } else if (script_type == "js" || script_type == "javascript") {
    command = "node " + script_path;
  } else if (script_type == "sh" || script_type == "bash") {
    command = "sh " + script_path;
  } else {
    result.error_message = "Unsupported script type: " + script_type;
    result.exit_code = -1;
    return result;
  }

  result = execute_command(command, sandbox_name);

  // Clean up temporary file
  std::filesystem::remove(script_path);

  return result;
}

SandboxResult SandboxService::execute_file(const std::string &file_path,
                                           const std::string &sandbox_name) {
  SandboxResult result;

  if (!std::filesystem::exists(file_path)) {
    result.error_message = "File not found: " + file_path;
    result.exit_code = -1;
    return result;
  }

  // Determine execution command based on file extension
  std::string extension = std::filesystem::path(file_path).extension().string();
  std::string command;

  if (extension == ".py") {
    command = "python " + file_path;
  } else if (extension == ".js") {
    command = "node " + file_path;
  } else if (extension == ".sh") {
    command = "sh " + file_path;
  } else {
    command = file_path; // Try to execute directly
  }

  return execute_command(command, sandbox_name);
}

bool SandboxService::copy_file_to_sandbox(const std::string &host_path,
                                          const std::string &container_path,
                                          const std::string &container_name) {
  std::string command = "docker cp " + escape_shell_arg(host_path) + " " +
                        container_name + ":" + escape_shell_arg(container_path);
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return false;

  // Read and discard output
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
  }

  int status = pclose(pipe);
#ifdef _WIN32
  return status == 0; // On Windows, pclose returns the exit status directly
#else
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool SandboxService::copy_file_from_sandbox(const std::string &container_path,
                                            const std::string &host_path,
                                            const std::string &container_name) {
  std::string command = "docker cp " + container_name + ":" +
                        escape_shell_arg(container_path) + " " +
                        escape_shell_arg(host_path);
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return false;

  // Read and discard output
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
  }

  int status = pclose(pipe);
#ifdef _WIN32
  return status == 0; // On Windows, pclose returns the exit status directly
#else
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

std::vector<std::string> SandboxService::list_active_containers() {
  std::vector<std::string> containers;

  FILE *pipe = Utils::Platform::open_process(
      "docker ps --filter name=agentware_sandbox_ --format '{{.Names}}'", "r");
  if (pipe) {
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string name(buffer);
      name.erase(name.find_last_not_of(" \n\r\t") + 1);
      if (!name.empty()) {
        containers.push_back(name);
      }
    }
    Utils::Platform::close_process(pipe);
  }

  return containers;
}

bool SandboxService::stop_container(const std::string &container_name) {
  std::string command = "docker stop " + escape_shell_arg(container_name);
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return false;

  // Read and discard output
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
  }

  int status = pclose(pipe);
#ifdef _WIN32
  return status == 0; // On Windows, pclose returns the exit status directly
#else
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool SandboxService::remove_container(const std::string &container_name) {
  std::string command = "docker rm -f " + escape_shell_arg(container_name);
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return false;

  // Read and discard output
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
  }

  int status = pclose(pipe);
#ifdef _WIN32
  return status == 0; // On Windows, pclose returns the exit status directly
#else
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

void SandboxService::cleanup_old_containers() {
  std::string command =
      "docker container prune -f --filter label=agentware_sandbox";
  FILE *pipe = popen(command.c_str(), "r");
  if (pipe) {
    // Read and discard output
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    }
    pclose(pipe);
  }
}

bool SandboxService::is_command_safe(const std::string &command) {
  // List of dangerous commands/patterns
  std::vector<std::string> dangerous_patterns = {
      "rm -rf",    "format",   "del /f",  "sudo",     "su -",      "chmod 777",
      "wget",      "curl",     "nc ",     "netcat",   "telnet",    "ssh",
      "scp",       "iptables", "ufw",     "firewall", "systemctl", "service",
      "mount",     "umount",   "fdisk",   "mkfs",     "dd if=",    "dd of=",
      ">/dev/",    "< /dev/",  "| /dev/", "& /dev/",  "$(",        "`",
      "eval",      "exec",     "source",  ".",        "bash -c",   "sh -c",
      "python -c", "perl -e",  "ruby -e"};

  std::string lower_command = command;
  std::transform(lower_command.begin(), lower_command.end(),
                 lower_command.begin(), ::tolower);

  for (const auto &pattern : dangerous_patterns) {
    if (lower_command.find(pattern) != std::string::npos) {
      return false;
    }
  }

  return true;
}

bool SandboxService::validate_sandbox_config(const SandboxConfig &config,
                                             std::string &error_message) {
  if (config.name.empty()) {
    error_message = "Sandbox name cannot be empty";
    return false;
  }

  if (config.image.empty()) {
    error_message = "Docker image cannot be empty";
    return false;
  }

  if (config.memory_limit_mb < 64 || config.memory_limit_mb > 8192) {
    error_message = "Memory limit must be between 64MB and 8192MB";
    return false;
  }

  if (config.cpu_limit_percent < 1 || config.cpu_limit_percent > 100) {
    error_message = "CPU limit must be between 1% and 100%";
    return false;
  }

  if (config.timeout_seconds < 1 || config.timeout_seconds > 3600) {
    error_message = "Timeout must be between 1 and 3600 seconds";
    return false;
  }

  return true;
}

std::vector<std::string>
SandboxService::get_security_warnings(const std::string &command) {
  std::vector<std::string> warnings;

  if (command.find("sudo") != std::string::npos) {
    warnings.push_back("Command contains 'sudo' - elevated privileges");
  }

  if (command.find("rm") != std::string::npos) {
    warnings.push_back("Command contains 'rm' - file deletion");
  }

  if (command.find("wget") != std::string::npos ||
      command.find("curl") != std::string::npos) {
    warnings.push_back("Command contains network access tools");
  }

  return warnings;
}

bool SandboxService::save_sandbox_config() {
  try {
    ensure_sandbox_directory();

    nlohmann::json config;
    config["sandboxes"] = nlohmann::json::object();

    for (const auto &[name, sandbox] : sandbox_configs_) {
      nlohmann::json sandbox_json;
      sandbox_json["name"] = sandbox.name;
      sandbox_json["image"] = sandbox.image;
      sandbox_json["volumes"] = sandbox.volumes;
      sandbox_json["environment_vars"] = sandbox.environment_vars;
      sandbox_json["allowed_commands"] = sandbox.allowed_commands;
      sandbox_json["memory_limit_mb"] = sandbox.memory_limit_mb;
      sandbox_json["cpu_limit_percent"] = sandbox.cpu_limit_percent;
      sandbox_json["timeout_seconds"] = sandbox.timeout_seconds;
      sandbox_json["network_access"] = sandbox.network_access;
      sandbox_json["working_directory"] = sandbox.working_directory;

      config["sandboxes"][name] = sandbox_json;
    }

    std::ofstream file(get_sandbox_config_path());
    file << config.dump(2);

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to save sandbox config: " << e.what() << std::endl;
    return false;
  }
}

bool SandboxService::load_sandbox_config() {
  try {
    std::string config_path = get_sandbox_config_path();
    if (!std::filesystem::exists(config_path)) {
      return true; // Use defaults
    }

    std::ifstream file(config_path);
    nlohmann::json config;
    file >> config;

    if (config.contains("sandboxes")) {
      for (const auto &[name, sandbox_json] : config["sandboxes"].items()) {
        // Skip built-in configs
        if (name == "default" || name == "python" || name == "nodejs" ||
            name == "shell") {
          continue;
        }

        SandboxConfig sandbox;
        sandbox.name = sandbox_json.value("name", name);
        sandbox.image = sandbox_json.value("image", "ubuntu:22.04");
        sandbox.memory_limit_mb = sandbox_json.value("memory_limit_mb", 512);
        sandbox.cpu_limit_percent = sandbox_json.value("cpu_limit_percent", 50);
        sandbox.timeout_seconds = sandbox_json.value("timeout_seconds", 300);
        sandbox.network_access = sandbox_json.value("network_access", false);
        sandbox.working_directory =
            sandbox_json.value("working_directory", "/workspace");

        if (sandbox_json.contains("volumes")) {
          sandbox.volumes =
              sandbox_json["volumes"].get<std::vector<std::string>>();
        }

        if (sandbox_json.contains("environment_vars")) {
          sandbox.environment_vars =
              sandbox_json["environment_vars"]
                  .get<std::map<std::string, std::string>>();
        }

        if (sandbox_json.contains("allowed_commands")) {
          sandbox.allowed_commands =
              sandbox_json["allowed_commands"].get<std::vector<std::string>>();
        }

        sandbox_configs_[name] = sandbox;
      }
    }

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to load sandbox config: " << e.what() << std::endl;
    return false;
  }
}

bool SandboxService::update_sandbox_limits(const std::string &name,
                                           size_t memory_mb,
                                           size_t cpu_percent) {
  auto it = sandbox_configs_.find(name);
  if (it == sandbox_configs_.end()) {
    return false;
  }

  it->second.memory_limit_mb = memory_mb;
  it->second.cpu_limit_percent = cpu_percent;

  return save_sandbox_config();
}

bool SandboxService::check_docker_installation() {
  return is_docker_available();
}

std::string SandboxService::get_docker_version() {
  FILE *pipe = Utils::Platform::open_process(
      "docker --version" + Utils::Platform::get_shell_redirect(), "r");
  if (!pipe)
    return "Not available";

  char buffer[256];
  std::string version;
  if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    version = buffer;
    version.erase(version.find_last_not_of(" \n\r\t") + 1);
  }
  Utils::Platform::close_process(pipe);

  return version.empty() ? "Not available" : version;
}

bool SandboxService::pull_docker_image(const std::string &image) {
  std::string command = "docker pull " + escape_shell_arg(image);
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
    return false;

  // Read and discard output
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
  }

  int status = pclose(pipe);
#ifdef _WIN32
  return status == 0; // On Windows, pclose returns the exit status directly
#else
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

std::vector<std::string> SandboxService::list_available_images() {
  std::vector<std::string> images;

  FILE *pipe = Utils::Platform::open_process(
      "docker images --format '{{.Repository}}:{{.Tag}}'", "r");
  if (pipe) {
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string image(buffer);
      image.erase(image.find_last_not_of(" \n\r\t") + 1);
      if (!image.empty()) {
        images.push_back(image);
      }
    }
    Utils::Platform::close_process(pipe);
  }

  return images;
}
} // namespace Services
