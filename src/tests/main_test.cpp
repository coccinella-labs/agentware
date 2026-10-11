#include "version.h"
#include "memory_manager.h"
#include "search_service.h"
#include "validation.h"
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
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <numeric>
#include <set>
#include <sstream>
#include <string>

// Basic test to verify the testing framework works
TEST(BasicTest, SanityCheck) { EXPECT_EQ(1 + 1, 2); }

// Test for version functionality
TEST(VersionTest, VersionCommand) {
  const char *version = Version::get_version();
  EXPECT_STREQ(version, agentware_version_string);
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
