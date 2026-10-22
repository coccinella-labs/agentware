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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

// Runs the agent with the given stdin inside the given directory and
// returns what it printed.
std::string run_agent(const fs::path &workdir, const fs::path &home,
                      const std::string &input) {
  const fs::path input_file = workdir / "stdin.txt";
  {
    std::ofstream out(input_file);
    out << input;
  }
  // The home directory is redirected as well, so the agent never touches
  // the real home of whoever runs the test. The variables are set in this
  // process and the child inherits them: the "NAME=value command" form
  // is not understood by the Windows shell.
  const char *old_home = std::getenv("HOME");
  const char *old_userprofile = std::getenv("USERPROFILE");
  const std::string saved_home = old_home ? old_home : "";
  const std::string saved_userprofile = old_userprofile ? old_userprofile : "";
  Utils::Platform::set_env("HOME", home.string().c_str());
  Utils::Platform::set_env("USERPROFILE", home.string().c_str());

  // popen and pclose are _popen and _pclose on Windows, so the platform
  // wrappers are used rather than the POSIX names.
  const std::string cmd = "cd \"" + workdir.string() + "\" && \"" +
                          std::string(AGENTWARE_BINARY_PATH) + "\" < \"" +
                          input_file.string() + "\" 2>&1";
  std::string output;
  FILE *pipe = Utils::Platform::open_process(cmd.c_str(), "r");
  if (pipe != nullptr) {
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      output += buffer;
    }
    Utils::Platform::close_process(pipe);
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
  return strip_escapes(output);
}

class CliBehavior : public ::testing::Test {
protected:
  void SetUp() override {
#ifdef _WIN32
    GTEST_SKIP() << "the driver uses a POSIX shell redirect";
#endif
    const std::string name =
        std::string(::testing::UnitTest::GetInstance()->current_test_info()->name());
    dir_ = fs::temp_directory_path() /
           ("agentware-cli-" + std::to_string(::getpid()) + "-" + name);
    home_ = dir_ / "home";
    fs::remove_all(dir_);
    fs::create_directories(home_);
  }

  void TearDown() override {
    std::error_code ignored;
    fs::remove_all(dir_, ignored);
  }

  std::string run(const std::string &input) { return run_agent(dir_, home_, input); }

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

} // namespace
