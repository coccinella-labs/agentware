// Behavior tests for the plain interactive surface of the agent.
//
// The golden suites cover the tool-call loop (JSON over the provider)
// and the command routing. What nothing covered was the ordinary
// interactive surface: the help text, the version banner and the
// legacy write:/read: round trip.
//
// The old preflight script claimed to cover exactly these, but it only
// compared exit codes, and the input it fed was consumed by the model
// prompt, so the commands never ran at all. These tests assert on the
// output, and they assert on the file that was written.
//
// The agent asks two questions before it accepts a command: the mode
// (1 online, 2 offline) and then the offline model. Every run here
// answers both, otherwise the next line is eaten as a model choice.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "version.h"
// platform.h includes windows.h on Windows, and windows.h defines the
// ERROR macro, which breaks the enum in theme_service.h. It comes last.
#include "platform.h"

#ifndef AGENTWARE_BINARY_PATH
#error "AGENTWARE_BINARY_PATH must point at the agent binary"
#endif

namespace {

namespace fs = std::filesystem;

// Removes the escape sequences the agent writes for color, so the
// assertions can look for plain text.
std::string strip_escapes(const std::string &input) {
  std::string clean;
  for (size_t i = 0; i < input.size();) {
    if (input[i] == '\x1b' && i + 1 < input.size() && input[i + 1] == '[') {
      size_t j = i + 2;
      while (j < input.size() && input[j] != 'm' &&
             !(input[j] >= '@' && input[j] <= '~')) {
        j++;
      }
      i = (j < input.size()) ? j + 1 : j;
      continue;
    }
    clean += input[i++];
  }
  return clean;
}

// Provider credentials are cleared before every run so a developer's real
// keys never leak into a test, and a test can set exactly what it needs.
constexpr const char *kProviderEnvVars[] = {
    "TOGETHER_API_KEY", "CEREBRAS_API_KEY", "FIREWORKS_API_KEY",
    "GROQ_API_KEY",     "DEEPSEEK_API_KEY", "OPENAI_API_KEY",
};

struct AgentRun {
  std::string output;
  int exit_code = -1;
};

// Runs the agent with the given stdin and extra argv inside the given
// directory, and returns its output plus exit code.
AgentRun
run_agent_ex(const fs::path &workdir, const fs::path &home,
             const std::string &input, const std::vector<std::string> &args,
             const std::vector<std::pair<std::string, std::string>> &env) {
  const fs::path input_file = workdir / "stdin.txt";
  {
    std::ofstream out(input_file);
    out << input;
  }

  const char *old_home = std::getenv("HOME");
  const char *old_userprofile = std::getenv("USERPROFILE");
  const std::string saved_home = old_home ? old_home : "";
  const std::string saved_userprofile = old_userprofile ? old_userprofile : "";

  std::vector<std::pair<std::string, std::optional<std::string>>> saved_env;
  for (const char *key : kProviderEnvVars) {
    const char *value = std::getenv(key);
    saved_env.push_back(
        {key, value ? std::optional<std::string>(value) : std::nullopt});
    Utils::Platform::unset_env(key);
  }
  for (const auto &entry : env) {
    Utils::Platform::set_env(entry.first.c_str(), entry.second.c_str());
  }

  Utils::Platform::set_env("HOME", home.string().c_str());
  Utils::Platform::set_env("USERPROFILE", home.string().c_str());

  std::string cmd =
      "cd \"" + workdir.string() + "\" && \"" + AGENTWARE_BINARY_PATH + "\"";
  for (const auto &arg : args) {
    cmd += " \"" + arg + "\"";
  }
  cmd += " < \"" + input_file.string() + "\" 2>&1";

  std::string output;
  int status = -1;
  FILE *pipe = Utils::Platform::open_process(cmd.c_str(), "r");
  if (pipe != nullptr) {
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      output += buffer;
    }
    status = Utils::Platform::close_process(pipe);
  }

  int exit_code = -1;
#ifdef _WIN32
  exit_code = status;
#else
  if (status != -1 && WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  }
#endif

  for (const auto &entry : saved_env) {
    if (entry.second.has_value()) {
      Utils::Platform::set_env(entry.first.c_str(), entry.second->c_str());
    } else {
      Utils::Platform::unset_env(entry.first.c_str());
    }
  }

  if (old_home != nullptr) {
    Utils::Platform::set_env("HOME", saved_home.c_str());
  } else {
    Utils::Platform::unset_env("HOME");
  }
  if (old_userprofile != nullptr) {
    Utils::Platform::set_env("USERPROFILE", saved_userprofile.c_str());
  } else {
    Utils::Platform::unset_env("USERPROFILE");
  }
  return {strip_escapes(output), exit_code};
}

// Convenience wrapper for the tests that only feed stdin.
std::string run_agent(const fs::path &workdir, const fs::path &home,
                      const std::string &input) {
  return run_agent_ex(workdir, home, input, {}, {}).output;
}

class CliBehavior : public ::testing::Test {
protected:
  void SetUp() override {
#ifdef _WIN32
    GTEST_SKIP() << "the driver uses a POSIX shell redirect";
#else
    // Keep the workspace name unique per test without POSIX-only APIs.
    static std::atomic<unsigned> counter{0};
    const std::string name = std::string(
        ::testing::UnitTest::GetInstance()->current_test_info()->name());
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    dir_ = fs::temp_directory_path() /
           ("agentware-cli-" + std::to_string(stamp) + "-" +
            std::to_string(counter++) + "-" + name);
    home_ = dir_ / "home";
    fs::remove_all(dir_);
    fs::create_directories(home_);
#endif
  }

  void TearDown() override {
    std::error_code ignored;
    fs::remove_all(dir_, ignored);
  }

  std::string run(const std::string &input) {
    return run_agent(dir_, home_, input);
  }

  AgentRun
  run_args(const std::string &input, const std::vector<std::string> &args = {},
           const std::vector<std::pair<std::string, std::string>> &env = {}) {
    return run_agent_ex(dir_, home_, input, args, env);
  }

  fs::path dir_;
  fs::path home_;
};

TEST_F(CliBehavior, HelpPrintsTheCommandList) {
  const std::string out = run("2\n1\nhelp\nexit\n");

  // The help handler has to have run: the command list is its output.
  EXPECT_NE(out.find("Commands"), std::string::npos) << out;
  EXPECT_NE(out.find("write:<file>"), std::string::npos) << out;
  EXPECT_NE(out.find("read:<file>"), std::string::npos) << out;
  // The version banner belongs to the version command, not to help.
  EXPECT_EQ(out.find("Agentware v"), std::string::npos) << out;
}

TEST_F(CliBehavior, VersionPrintsTheLibraryVersion) {
  const std::string out = run("2\n1\nversion\nexit\n");

  // The version the agent prints is the version the library reports,
  // so a stale or hardcoded string cannot pass.
  EXPECT_NE(out.find(std::string("Agentware v") + Version::get_version()),
            std::string::npos)
      << out;
  EXPECT_NE(out.find("Built:"), std::string::npos) << out;
}

TEST_F(CliBehavior, WriteThenReadRoundTrip) {
  const std::string out =
      run("2\n1\nwrite:probe.txt hello round trip\nread:probe.txt\nexit\n");

  // The write has to be reported as done.
  EXPECT_NE(out.find("written successfully"), std::string::npos) << out;
  // The read has to give the content back.
  EXPECT_NE(out.find("hello round trip"), std::string::npos) << out;

  // And the file has to hold exactly that content on disk.
  const fs::path probe = dir_ / "probe.txt";
  ASSERT_TRUE(fs::exists(probe)) << out;
  std::ifstream in(probe);
  std::stringstream buffer;
  buffer << in.rdbuf();
  EXPECT_EQ(buffer.str(), "hello round trip");
}

TEST_F(CliBehavior, VersionFlagPrintsAndExits) {
  const AgentRun run = run_args("", {"--version"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(
      run.output.find(std::string("Agentware v") + Version::get_version()),
      std::string::npos)
      << run.output;
  EXPECT_NE(run.output.find("Built:"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, ShortVersionFlagPrintsAndExits) {
  const AgentRun run = run_args("", {"-V"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(
      run.output.find(std::string("Agentware v") + Version::get_version()),
      std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, HelpFlagPrintsUsage) {
  const AgentRun run = run_args("", {"--help"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Usage: agentware-agent"), std::string::npos)
      << run.output;
  EXPECT_NE(run.output.find("--mode"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, ShortHelpFlagPrintsUsage) {
  const AgentRun run = run_args("", {"-H"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Usage: agentware-agent"), std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, UnknownFlagFailsWithUsage) {
  const AgentRun run = run_args("", {"--nope"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Error: unknown option: --nope"), std::string::npos)
      << run.output;
  EXPECT_NE(run.output.find("Usage: agentware-agent"), std::string::npos)
      << run.output;
  EXPECT_EQ(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, MissingFlagValueFails) {
  const AgentRun run = run_args("", {"--mode"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("missing value for --mode"), std::string::npos)
      << run.output;
  EXPECT_NE(run.output.find("Usage: agentware-agent"), std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, InvalidModeFails) {
  const AgentRun run = run_args("", {"--mode", "bogus"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Error: invalid mode: bogus"), std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, UnknownProviderFails) {
  const AgentRun run = run_args("", {"--mode", "online", "--provider", "nope"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Error: unknown provider: nope"), std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, UnknownModelFails) {
  const AgentRun run =
      run_args("", {"--mode", "offline", "--model", "not-a-model"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Error: unknown model: not-a-model"),
            std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, ProviderWithoutModeFails) {
  const AgentRun run = run_args("", {"--provider", "together"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("--provider requires --mode online"),
            std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, ModelWithoutModeFails) {
  const AgentRun run = run_args("", {"--model", "llama3.2:3b"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("--model requires --mode offline"),
            std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, OnlineWithModelIsRejected) {
  const AgentRun run =
      run_args("", {"--mode", "online", "--model", "llama3.2:3b"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("--model is offline-only"), std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, OfflineWithProviderIsRejected) {
  const AgentRun run =
      run_args("", {"--mode", "offline", "--provider", "together"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("--provider is online-only"), std::string::npos)
      << run.output;
}

TEST_F(CliBehavior, OfflineModelSkipsBothPrompts) {
  const AgentRun run =
      run_args("exit\n", {"--mode", "offline", "--model", "llama3.2:3b"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Mode=Offline"), std::string::npos) << run.output;
  EXPECT_NE(run.output.find("llama3.2:3b"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Mode ["), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Model ["), std::string::npos) << run.output;
  EXPECT_NE(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, OfflineModeWithoutModelStillPromptsModel) {
  const AgentRun run = run_args("1\nexit\n", {"--mode", "offline"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_EQ(run.output.find("Mode ["), std::string::npos) << run.output;
  EXPECT_NE(run.output.find("Model ["), std::string::npos) << run.output;
  EXPECT_NE(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, OnlineProviderSkipsBothPrompts) {
  const AgentRun run =
      run_args("exit\n", {"--mode", "online", "--provider", "together"},
               {{"TOGETHER_API_KEY", "test-key"}});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Mode=Online"), std::string::npos) << run.output;
  EXPECT_NE(run.output.find("Together AI"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Mode ["), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Provider ["), std::string::npos) << run.output;
  EXPECT_NE(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, OnlineProviderWithoutKeyFails) {
  const AgentRun run =
      run_args("", {"--mode", "online", "--provider", "together"});
  EXPECT_NE(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("TOGETHER_API_KEY is not set"), std::string::npos)
      << run.output;
  EXPECT_EQ(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, ModePromptEofExitsCleanly) {
  const AgentRun run = run_args("");
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Goodbye"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, ProviderPromptEofExitsCleanly) {
  const AgentRun run = run_args("", {"--mode", "online"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Goodbye"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Ready -"), std::string::npos) << run.output;
}

TEST_F(CliBehavior, ModelPromptEofExitsCleanly) {
  const AgentRun run = run_args("", {"--mode", "offline"});
  EXPECT_EQ(run.exit_code, 0) << run.output;
  EXPECT_NE(run.output.find("Goodbye"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("Ready -"), std::string::npos) << run.output;
}

} // namespace
