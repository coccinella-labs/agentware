#include "sandbox_service.h"
#include "platform.h"
#include "validation.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
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
  // The agent's working directory is mounted read-write at /workspace so
  // sandboxed commands can see the project. Mounts stay limited to this
  // directory: anything else must come from an explicitly validated
  // custom config, never by accident.
  std::string workspace_volume;
  {
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
      workspace_volume = cwd.string() + ":/workspace:rw";
    }
  }

  // Default sandbox for general use
  SandboxConfig default_config;
  default_config.name = "default";
  // NOTE: mutable tags are trust-on-first-pull. Digest pinning is deferred
  // to an owner decision on image-update policy; see the plan doc.
  default_config.image = "ubuntu:22.04";
  default_config.memory_limit_mb = 512;
  default_config.cpu_limit_percent = 50;
  default_config.timeout_seconds = 300;
  default_config.network_access = false;
  default_config.working_directory = "/workspace";
  default_config.allowed_commands = {"ls",   "cat",  "echo", "grep",
                                     "find", "head", "tail", "wc"};
  if (!workspace_volume.empty()) {
    default_config.volumes = {workspace_volume};
  }
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
  if (!workspace_volume.empty()) {
    python_config.volumes = {workspace_volume};
  }
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
  if (!workspace_volume.empty()) {
    node_config.volumes = {workspace_volume};
  }
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
  if (!workspace_volume.empty()) {
    shell_config.volumes = {workspace_volume};
  }
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

SandboxService::DockerStatus
SandboxService::probe_docker(int timeout_seconds) {
  // A present CLI binary is not evidence that the sandbox can proceed: with
  // the daemon stopped the CLI still answers --version, which would let the UI
  // claim isolation it cannot provide. Query the daemon instead, within a
  // finite bound so a wedged daemon cannot block the agent.
  SpawnedResult result = spawn_capture({"docker", "info"}, timeout_seconds);
  if (result.timed_out) {
    return DockerStatus::TimedOut;
  }
  return result.exit_code == 0 ? DockerStatus::Available
                               : DockerStatus::Unavailable;
}

bool SandboxService::is_docker_available() {
  return probe_docker(kDockerProbeTimeoutSeconds) == DockerStatus::Available;
}

SpawnedResult SandboxService::spawn_capture(
    const std::vector<std::string> &argv, int timeout_seconds) {
  SpawnedResult result;
  if (argv.empty()) {
    result.exit_code = -1;
    return result;
  }
#ifdef _WIN32
  // Deterministic CRT quoting (CommandLineToArgvW inverse): backslashes
  // are doubled before quotes and at the end; the whole argument is
  // wrapped. No shell, no metacharacters.
  std::string command_line;
  for (const auto &arg : argv) {
    if (!command_line.empty()) {
      command_line += ' ';
    }
    command_line += '"';
    size_t backslashes = 0;
    for (char c : arg) {
      if (c == '\\') {
        ++backslashes;
        command_line += '\\';
      } else if (c == '"') {
        command_line.append(backslashes + 1, '\\');
        command_line += '"';
        backslashes = 0;
      } else {
        backslashes = 0;
        command_line += c;
      }
    }
    command_line.append(backslashes * 2, '\\');
    command_line += '"';
  }
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE read_handle = nullptr;
  HANDLE write_handle = nullptr;
  if (!CreatePipe(&read_handle, &write_handle, &attributes, 0)) {
    result.exit_code = -1;
    return result;
  }
  SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_handle;
  startup.hStdError = write_handle;
  startup.hStdInput = nullptr;
  PROCESS_INFORMATION proc{};
  ZeroMemory(&proc, sizeof(proc));
  std::vector<char> command_buffer(command_line.begin(), command_line.end());
  command_buffer.push_back('\0');
  BOOL started =
      CreateProcessA(nullptr, command_buffer.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &startup, &proc);
  CloseHandle(write_handle);
  if (!started) {
    CloseHandle(read_handle);
    result.exit_code = -1;
    return result;
  }
  std::string output;
  char buffer[4096];
  bool timed_out = false;
  if (timeout_seconds > 0) {
    // Poll rather than block so a stalled child cannot pin the agent.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(timeout_seconds);
    for (;;) {
      DWORD available = 0;
      if (PeekNamedPipe(read_handle, nullptr, 0, nullptr, &available, nullptr) &&
          available > 0) {
        DWORD got = 0;
        if (ReadFile(read_handle, buffer, sizeof(buffer), &got, nullptr) &&
            got > 0) {
          output.append(buffer, got);
        }
        continue;
      }
      if (WaitForSingleObject(proc.hProcess, 0) == WAIT_OBJECT_0) {
        break; // Exited: drain whatever remains.
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        break;
      }
      Sleep(5);
    }
    CloseHandle(read_handle);
    if (timed_out) {
      TerminateProcess(proc.hProcess, 1);
    }
  } else {
    DWORD got = 0;
    while (ReadFile(read_handle, buffer, sizeof(buffer), &got, nullptr) &&
           got > 0) {
      output.append(buffer, got);
    }
    CloseHandle(read_handle);
  }
  if (!timed_out) {
    WaitForSingleObject(proc.hProcess, INFINITE);
  }
  DWORD code = 0;
  if (!GetExitCodeProcess(proc.hProcess, &code)) {
    code = static_cast<DWORD>(-1);
  }
  CloseHandle(proc.hProcess);
  CloseHandle(proc.hThread);
  result.output = output;
  result.timed_out = timed_out;
  result.exit_code = timed_out ? -1 : static_cast<int>(code);
  return result;
#else
  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) {
    result.exit_code = -1;
    return result;
  }
  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    result.exit_code = -1;
    return result;
  }
  if (pid == 0) {
    if (timeout_seconds > 0) {
      // Own process group, so a timeout can kill the whole tree rather than
      // just this process. Unbounded callers keep the inherited group so
      // their existing signal behavior is unchanged.
      //
      // Known behavior change, accepted rather than fixed here: because the
      // child no longer shares the terminal's foreground process group, a
      // terminal Ctrl-C no longer reaches it. The agent installs no signal
      // handler, so Ctrl-C still terminates the agent; what can now linger is
      // the local docker client, until its container exits. Nothing
      // user-visible is affected either way: the daemon owns the container,
      // and --rm removes it when the command exits, so killing the client
      // never provided container cleanup. This applies to the POSIX path
      // only. The Windows branch below is unchanged and does not create a
      // process group at all.
      setpgid(0, 0);
    }
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    std::vector<char *> exec_args;
    exec_args.reserve(argv.size() + 1);
    for (const auto &arg : argv) {
      exec_args.push_back(const_cast<char *>(arg.c_str()));
    }
    exec_args.push_back(nullptr);
    execvp(exec_args[0], exec_args.data());
    _exit(127);
  }
  close(pipefd[1]);
  if (timeout_seconds > 0) {
    // Mirror the child's setpgid here so the group is established before the
    // timeout can fire, whichever call wins the race.
    setpgid(pid, pid);
  }
  std::string output;
  char buffer[4096];
  bool timed_out = false;

  if (timeout_seconds > 0) {
    // Drain without blocking so a stalled child cannot pin the agent. The
    // pipe is polled against a deadline; on expiry the child is killed and
    // reaped, and the result is reported as a failure rather than a success.
    const int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
      fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(timeout_seconds);
    for (;;) {
      // Checked before the read as well as after it. A descendant that
      // inherits the pipe and keeps writing would otherwise keep the read
      // loop returning data and postpone the bound indefinitely.
      if (std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        break;
      }
      const ssize_t count = read(pipefd[0], buffer, sizeof(buffer));
      if (count > 0) {
        output.append(buffer, static_cast<size_t>(count));
        continue;
      }
      if (count == 0) {
        break; // EOF: the child closed its end.
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  } else {
    // Unbounded: drain until EOF, as before. The pipe must stay open until
    // the child finishes writing, otherwise it would die of SIGPIPE instead
    // of exiting normally.
    ssize_t count = 0;
    while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
      output.append(buffer, static_cast<size_t>(count));
    }
  }

  close(pipefd[0]);
  if (timed_out) {
    // Kill the whole process group so a wedged command's descendants do not
    // survive. Signalling only the direct child would orphan them.
    kill(-pid, SIGKILL);
    // Then signal the direct child unconditionally. The group kill above is
    // ignored if setpgid never took effect, and the waitpid below is blocking
    // with no WNOHANG, so an unkilled child would park the worker forever and
    // reintroduce the thread leak this bound exists to prevent. When the group
    // did form this second call is a harmless no-op; it is deliberately not
    // gated on the group's success, because reaping must not depend on it.
    // Issued before waitpid, while the pid is still unreaped and so cannot
    // have been recycled.
    kill(pid, SIGKILL);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  result.output = output;
  result.timed_out = timed_out;
  result.exit_code =
      timed_out ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
  return result;
#endif
}

BoundedResult SandboxService::run_bounded(
    const std::vector<std::string> &argv,
    const std::vector<std::string> &stop_argv, int timeout_seconds,
    int stop_timeout_seconds) {
  BoundedResult result;
  std::promise<SpawnedResult> done;
  std::future<SpawnedResult> finished = done.get_future();
  // The worker is given the same deadline as the caller waits for. Without
  // it, a wedged command leaves this thread blocked forever inside
  // spawn_capture's drain loop: the loop only ends at EOF, which needs every
  // write end of the pipe closed, which needs the whole process tree gone.
  // Bounding the spawn is what lets the worker finish and release its
  // captures; the detached thread then exits on its own instead of leaking.
  std::thread(
      [promise = std::move(done), argv, timeout_seconds]() mutable {
        promise.set_value(spawn_capture(argv, timeout_seconds));
      })
      .detach();
  if (finished.wait_for(std::chrono::seconds(timeout_seconds)) ==
      std::future_status::ready) {
    SpawnedResult spawned = finished.get();
    result.output = spawned.output;
    result.exit_code = spawned.exit_code;
    if (spawned.timed_out) {
      // The worker's own deadline fired at effectively the same moment as the
      // wait above, so this is a timeout, not a plain failure. Reporting it as
      // an ordinary result would hide it.
      result.timed_out = true;
      result.error_message =
          "Command execution timed out; container stop requested";
    }
    return result;
  }
  // Deadline exceeded: bounded stop attempt, then a grace reap. If the
  // daemon is wedged the wait below expires and control still returns.
  std::promise<SpawnedResult> stop_done;
  std::future<SpawnedResult> stop_finished = stop_done.get_future();
  std::thread(
      [promise = std::move(stop_done), stop_argv]() mutable {
        promise.set_value(spawn_capture(stop_argv));
      })
      .detach();
  stop_finished.wait_for(std::chrono::seconds(stop_timeout_seconds));
  constexpr int kReapGraceSeconds = 10;
  if (finished.wait_for(std::chrono::seconds(kReapGraceSeconds)) ==
      std::future_status::ready) {
    SpawnedResult spawned = finished.get();
    result.output = spawned.output;
    result.exit_code = spawned.exit_code;
    result.timed_out = true;
    result.error_message = "Command execution timed out; container stop requested";
    return result;
  }
  result.timed_out = true;
  result.error_message =
      "Command execution timed out; container may still be running";
  result.exit_code = -1;
  return result;
}

void SandboxService::initialize() {
  initialize_default_configs();
  load_sandbox_config();
}

bool SandboxService::create_sandbox_config(const std::string &name,
                                           const SandboxConfig &config) {
  std::string error_message;
  if (!validate_sandbox_config(config, error_message)) {
    return false;
  }
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

  // Resolve the effective config (explicit working directory wins).
  SandboxConfig resolved = config;
  if (!working_dir.empty()) {
    resolved.working_directory = working_dir;
  }

  // Enforce configuration validation at the single execution choke point:
  // hostile configs must fail here even if they arrived via config file.
  // Every later step trusts this copy.
  std::string config_error;
  if (!validate_sandbox_config(resolved, config_error)) {
    result.error_message = "Invalid sandbox configuration: " + config_error;
    result.exit_code = -1;
    return result;
  }

  // Build the argv vector and run it bounded: on expiry run_bounded
  // stops the container and returns control to the caller.
  auto start_time = std::chrono::high_resolution_clock::now();
  std::string container_name = generate_container_name();
  std::vector<std::string> argv =
      build_docker_argv(resolved, command, container_name);
  BoundedResult bounded = run_bounded(
      argv, {"docker", "stop", container_name}, resolved.timeout_seconds, 15);

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  result.execution_time_seconds = duration.count() / 1000.0;
  result.stdout_output = bounded.output;
  result.exit_code = bounded.exit_code;
  result.timed_out = bounded.timed_out;
  result.error_message = bounded.error_message;
  return result;
}

std::vector<std::string> SandboxService::build_docker_argv(
    const SandboxConfig &config, const std::string &command,
    const std::string &container_name) {
  // Argv elements reach the child verbatim: no shell ever parses this
  // vector, so hostile values cannot break out regardless of content.
  std::vector<std::string> argv = {
      "docker",      "run",         "--rm",
      "--name",      container_name, "--memory",
      std::to_string(config.memory_limit_mb) + "m"};
  {
    std::ostringstream cpus;
    cpus << (config.cpu_limit_percent / 100.0);
    argv.push_back("--cpus");
    argv.push_back(cpus.str());
  }
  argv.insert(argv.end(), {"--cap-drop", "ALL", "--security-opt",
                           "no-new-privileges", "--pids-limit", "256"});

  if (!config.network_access) {
    argv.push_back("--network=none");
  }

  // Set working directory
  std::string work_dir = config.working_directory.empty()
                             ? "/workspace"
                             : config.working_directory;
  argv.push_back("--workdir=" + work_dir);

  // Add environment variables
  for (const auto &[key, value] : config.environment_vars) {
    argv.push_back("-e");
    argv.push_back(key + "=" + value);
  }

  // Add volumes
  for (const auto &volume : config.volumes) {
    argv.push_back("-v");
    argv.push_back(volume);
  }

  argv.push_back(config.image);
  argv.insert(argv.end(), {"sh", "-c", command});
  return argv;
}

std::string SandboxService::format_result(const SandboxResult &result) {
  std::string out = result.stdout_output;
  auto append_line = [&out](const std::string &line) {
    if (!out.empty()) {
      out += "\n";
    }
    out += line;
  };
  if (result.timed_out) {
    append_line("[Command timed out]");
  }
  if (result.exit_code != 0) {
    append_line("Exit code: " + std::to_string(result.exit_code));
  }
  if (!result.error_message.empty()) {
    append_line("Error: " + result.error_message);
  }
  return out.empty() ? "Command completed" : out;
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
  SpawnedResult result = spawn_capture(
      {"docker", "cp", host_path, container_name + ":" + container_path});
  return result.exit_code == 0;
}

bool SandboxService::copy_file_from_sandbox(const std::string &container_path,
                                            const std::string &host_path,
                                            const std::string &container_name) {
  SpawnedResult result = spawn_capture(
      {"docker", "cp", container_name + ":" + container_path, host_path});
  return result.exit_code == 0;
}

std::vector<std::string> SandboxService::list_active_containers() {
  std::vector<std::string> containers;
  SpawnedResult result = spawn_capture(
      {"docker", "ps", "--filter", "name=agentware_sandbox_", "--format",
       "{{.Names}}"});
  if (result.exit_code != 0) {
    return containers;
  }
  std::istringstream lines(result.output);
  std::string name;
  while (std::getline(lines, name)) {
    name.erase(name.find_last_not_of(" \n\r\t") + 1);
    if (!name.empty()) {
      containers.push_back(name);
    }
  }
  return containers;
}

bool SandboxService::stop_container(const std::string &container_name) {
  SpawnedResult result = spawn_capture({"docker", "stop", container_name});
  return result.exit_code == 0;
}

bool SandboxService::remove_container(const std::string &container_name) {
  SpawnedResult result = spawn_capture({"docker", "rm", "-f", container_name});
  return result.exit_code == 0;
}

void SandboxService::cleanup_old_containers() {
  spawn_capture(
      {"docker", "container", "prune", "-f", "--filter", "label=agentware_sandbox"});
}

bool SandboxService::is_command_safe(const std::string &command) {
  // Substring denylist: defense in depth only, never a substitute for
  // container isolation. NOTE: a bare "." entry was removed here — it
  // blocked nearly every realistic command containing a dot (e.g.
  // "cat README.md") while missing whole dangerous classes. Keep entries
  // structural; prove each one with a test before adding.
  std::vector<std::string> dangerous_patterns = {
      "rm -rf",    "format",   "del /f",  "sudo",     "su -",      "chmod 777",
      "wget",      "curl",     "nc ",     "netcat",   "telnet",    "ssh",
      "scp",       "iptables", "ufw",     "firewall", "systemctl", "service",
      "mount",     "umount",   "fdisk",   "mkfs",     "dd if=",    "dd of=",
      ">/dev/",    "< /dev/",  "| /dev/", "& /dev/",  "$(",        "`",
      "eval",      "exec",     "source",  "bash -c",  "sh -c",
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

namespace {
// Container paths are POSIX-style inside the image. A leading '/' (or a
// Windows drive prefix for Docker Desktop hosts) marks absolute.
bool is_absolute_container_path(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  if (path[0] == '/') {
    return true;
  }
  return path.size() > 2 && path[1] == ':' &&
         (path[2] == '/' || path[2] == '\\');
}

// Image references must be single tokens: quoting neutralizes shell
// metacharacters, but whitespace/control characters indicate a malformed
// or hostile value, so reject them outright.
bool is_safe_image_reference(const std::string &image,
                             std::string &error_message) {
  if (image.empty()) {
    error_message = "Docker image cannot be empty";
    return false;
  }
  for (unsigned char c : image) {
    if (std::isspace(c) || std::iscntrl(c)) {
      error_message = "Docker image contains invalid characters";
      return false;
    }
  }
  return true;
}

// Volume specs must be host:container[:options] with non-empty parts.
// Extra colons are rejected rather than misparsed (fail closed). Note a
// Windows drive letter (C:\...) contains a colon, so Windows-style host
// paths are rejected: run Docker Desktop hosts with POSIX-style mounts.
bool is_safe_volume_spec(const std::string &volume,
                         std::string &error_message) {
  std::vector<std::string> parts;
  size_t pos = 0;
  while (true) {
    size_t colon = volume.find(':', pos);
    parts.push_back(volume.substr(pos, colon == std::string::npos
                                           ? std::string::npos
                                           : colon - pos));
    if (colon == std::string::npos) {
      break;
    }
    pos = colon + 1;
  }
  if (parts.size() < 2 || parts.size() > 3 || parts[0].empty() ||
      parts[1].empty()) {
    error_message =
        "Volume must have host:container[:options] form: " + volume;
    return false;
  }
  if (!is_absolute_container_path(parts[1])) {
    error_message = "Volume container path must be absolute: " + volume;
    return false;
  }
  return true;
}
} // namespace

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

  if (config.working_directory.empty() ||
      !is_absolute_container_path(config.working_directory)) {
    error_message = "Working directory must be a non-empty absolute path";
    return false;
  }

  if (!is_safe_image_reference(config.image, error_message)) {
    return false;
  }

  for (const auto &volume : config.volumes) {
    if (!is_safe_volume_spec(volume, error_message)) {
      return false;
    }
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
  SpawnedResult result = spawn_capture({"docker", "--version"});
  if (result.exit_code != 0) {
    return "Not available";
  }
  std::string version;
  std::istringstream first_line(result.output);
  if (std::getline(first_line, version)) {
    version.erase(version.find_last_not_of(" \n\r\t") + 1);
  }
  return version.empty() ? "Not available" : version;
}

bool SandboxService::pull_docker_image(const std::string &image) {
  SpawnedResult result = spawn_capture({"docker", "pull", image});
  return result.exit_code == 0;
}

std::vector<std::string> SandboxService::list_available_images() {
  std::vector<std::string> images;
  SpawnedResult result = spawn_capture(
      {"docker", "images", "--format", "{{.Repository}}:{{.Tag}}"});
  if (result.exit_code != 0) {
    return images;
  }
  std::istringstream lines(result.output);
  std::string image;
  while (std::getline(lines, image)) {
    image.erase(image.find_last_not_of(" \n\r\t") + 1);
    if (!image.empty()) {
      images.push_back(image);
    }
  }
  return images;
}
} // namespace Services
