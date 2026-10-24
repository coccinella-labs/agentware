#pragma once
#include "config.h" // For AGENTWARE_API

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>

// Command dispatch boundary.
//
// These are free functions rather than Agent members so the routing rules can
// be unit tested directly. agent.h is deliberately not included by the test
// suite (see the note at the top of main_test.cpp), so anything tested at this
// boundary has to live outside the Agent class.
//
// The rules encoded here exist because prefix and substring matching let
// ordinary prose reach destructive handlers. Two examples that used to be
// mishandled:
//
//   "clear: the screen please"  wiped session memory
//   "!echo hello@world"         was diverted to chat by the '@'
//
// Both are covered by regression tests in the test suite.
namespace Core {

// Which handler family owns an input line.
enum class InputRoute {
  Chat,
  FileInjection,
  Shell,
  Meta,
  DirectCommand,
};

// Routes one already-trimmed input line. Explicit command prefixes ('!',
// '/'), the no-argument bare verbs, and colon commands outrank '@', so a
// command that merely contains an at-sign is never diverted to file
// injection.
[[nodiscard]] AGENTWARE_API InputRoute
classify_input(const std::string &trimmed);

// True only when `input` is exactly `verb`. These verbs accept no arguments,
// so "clear: the screen please" is prose rather than a command, and
// "clearance-notes: x" is not a near miss for "clear".
[[nodiscard]] AGENTWARE_API bool matches_bare_verb(const std::string &input,
                                                   const std::string &verb);

// Case-insensitive prefix test for colon tool prefixes. "Search:cmake" must
// reach the search tool exactly like "search:cmake", but tool arguments keep
// their original casing: the caller slices `input` at prefix.size() rather
// than matching against a lowercased copy, because replacing text or writing
// content is case-sensitive.
[[nodiscard]] AGENTWARE_API bool has_prefix_ci(const std::string &input,
                                               const std::string &prefix);

// True when the '@' at `pos` opens a file reference, meaning it starts the
// input or follows whitespace or an opening quote. Keeping this boundary
// explicit is what stops an email address from being read as a file name.
[[nodiscard]] AGENTWARE_API bool is_file_reference_at(const std::string &input,
                                                      size_t pos);

// Runs fn with a timeout. Returns true if fn completed within timeout_sec,
// false if it timed out (fn may still be running in a detached thread).
// Used by the model tool-call loop for per-tool timeouts.
inline bool run_with_timeout(std::function<void()> fn, int timeout_sec) {
  std::atomic<bool> done{false};
  std::thread t([&]() {
    fn();
    done = true;
  });
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
  while (!done.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (done.load()) {
    t.join();
    return true;
  }
  t.detach();
  return false;
}

// Removes secrets from a command line before it reaches memory, logs, or the
// checkpoint copies of memory. Currently redacts the value passed to
// "/auth key <provider> <secret>".
[[nodiscard]] AGENTWARE_API std::string
redact_secrets(const std::string &input);

} // namespace Core
