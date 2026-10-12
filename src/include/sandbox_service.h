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
  // Informational only: execution does not consult this list. Nothing
  // user-visible may claim otherwise.
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

// Result of spawning one process with output captured. No shell is
// involved on any platform, so argv elements reach the child verbatim.
struct SpawnedResult {
  std::string output{};
  int exit_code{-1};
  // True when a positive timeout_seconds elapsed and the child was killed.
  // exit_code is then -1, never a success value.
  bool timed_out{false};
};

// Result of a deadline-bounded spawn. timed_out implies the stop command
// was attempted; error_message explains the outcome in every case.
struct BoundedResult {
  std::string output{};
  int exit_code{-1};
  bool timed_out{false};
  std::string error_message{};
};

class SandboxService {
private:
  static std::map<std::string, SandboxConfig> sandbox_configs_;
  static std::string get_sandbox_config_path();
  static void ensure_sandbox_directory();
  static void initialize_default_configs();
  static std::string generate_container_name();

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

  // Outcome of a Docker availability probe. Availability is only ever
  // reported when the daemon actually answered.
  enum class DockerStatus {
    Available,
    Unavailable,
    TimedOut,
  };

  // Seconds allowed for the availability probe. A healthy daemon answers
  // "docker info" in well under a second; the bound exists so a stalled or
  // wedged daemon cannot block the agent indefinitely.
  static constexpr int kDockerProbeTimeoutSeconds = 5;

  static bool is_docker_available();
  // Probes the daemon within timeout_seconds and reports which of the three
  // outcomes occurred, so callers can explain a timeout rather than reporting
  // a bare "unavailable".
  static DockerStatus probe_docker(int timeout_seconds = kDockerProbeTimeoutSeconds);
  // Process spawning without a host shell: POSIX fork/execvp, Windows
  // CreateProcess with deterministic CRT quoting. Declared public as
  // documented test seams; see the timeout and argv tests.
  // timeout_seconds <= 0 preserves the original unbounded behavior.
  static SpawnedResult
  spawn_capture(const std::vector<std::string> &argv,
                int timeout_seconds = 0);
  static BoundedResult
  run_bounded(const std::vector<std::string> &argv,
              const std::vector<std::string> &stop_argv, int timeout_seconds,
              int stop_timeout_seconds);
  static std::vector<std::string>
  build_docker_argv(const SandboxConfig &config, const std::string &command,
                    const std::string &container_name);
  // Security validation
  static bool is_command_safe(const std::string &command);
  // Renders a SandboxResult the way callers display it. Pure.
  static std::string format_result(const SandboxResult &result);
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
