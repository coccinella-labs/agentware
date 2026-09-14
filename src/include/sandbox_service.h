#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Services {

struct SandboxConfig {
  std::string name;
  std::string image;
  std::vector<std::string> volumes;
  std::map<std::string, std::string> environment_vars;
  std::vector<std::string> allowed_commands;
  size_t memory_limit_mb = 512;
  size_t cpu_limit_percent = 50;
  int timeout_seconds = 300;
  bool network_access = false;
  std::string working_directory = "/workspace";
};

struct SandboxResult {
  int exit_code;
  std::string stdout_output;
  std::string stderr_output;
  bool timed_out = false;
  double execution_time_seconds = 0.0;
  std::string error_message;
};

class SandboxService {
private:
  static std::map<std::string, SandboxConfig> sandbox_configs_;
  static std::string get_sandbox_config_path();
  static void ensure_sandbox_directory();
  static void initialize_default_configs();
  static std::string generate_container_name();
  static bool is_docker_available();
  static std::string escape_shell_arg(const std::string &arg);

public:
  // Sandbox management
  static void initialize();
  static bool create_sandbox_config(const std::string &name,
                                    const SandboxConfig &config);
  static bool remove_sandbox_config(const std::string &name);
  static std::vector<std::string> list_sandbox_configs();
  static SandboxConfig get_sandbox_config(const std::string &name);

  // Command execution
  static SandboxResult
  execute_command(const std::string &command,
                  const std::string &sandbox_name = "default",
                  const std::string &working_dir = "");
  static SandboxResult
  execute_script(const std::string &script_content,
                 const std::string &script_type,
                 const std::string &sandbox_name = "default");
  static SandboxResult
  execute_file(const std::string &file_path,
               const std::string &sandbox_name = "default");

  // File operations
  static bool copy_file_to_sandbox(const std::string &host_path,
                                   const std::string &container_path,
                                   const std::string &container_name);
  static bool copy_file_from_sandbox(const std::string &container_path,
                                     const std::string &host_path,
                                     const std::string &container_name);

  // Container management
  static std::vector<std::string> list_active_containers();
  static bool stop_container(const std::string &container_name);
  static bool remove_container(const std::string &container_name);
  static void cleanup_old_containers();

  // Security validation
  static bool is_command_safe(const std::string &command);
  static bool validate_sandbox_config(const SandboxConfig &config,
                                      std::string &error_message);
  static std::vector<std::string>
  get_security_warnings(const std::string &command);

  // Configuration management
  static bool save_sandbox_config();
  static bool load_sandbox_config();
  static bool update_sandbox_limits(const std::string &name, size_t memory_mb,
                                    size_t cpu_percent);

  // Utility functions
  static bool check_docker_installation();
  static std::string get_docker_version();
  static bool pull_docker_image(const std::string &image);
  static std::vector<std::string> list_available_images();
};
} // namespace Services
