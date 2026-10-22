#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "file_service.h"

namespace Services {

// Which external search tools were found on PATH at startup. All false means
// "use the native fallback for everything", which must always work.
struct SearchCapabilities {
  bool has_ripgrep = false;  // rg
  bool has_fd = false;       // fd or fdfind
  bool has_ast_grep = false; // sg
  bool has_jq = false;
  bool has_git = false;
  // Which binary provides fd: "fd", "fdfind", or empty when absent.
  std::string fd_binary;
};

// Scans PATH once. Never throws, never touches the network, takes
// microseconds. Called once at agent startup; the result lives on the Agent.
SearchCapabilities detect_search_capabilities();

// Test seam: same detection over an explicit PATH string, so tests need no
// environment manipulation. detect_search_capabilities() reads $PATH and
// delegates here.
SearchCapabilities
detect_search_capabilities_from_path(const std::string &path_env);

struct SearchOptions {
  std::string directory = ".";
  std::string pattern;
  std::string file_filter = "*";
  bool case_sensitive = false;
  std::size_t max_matches = 200;
  std::size_t max_bytes = 1 << 20; // 1 MiB of serialized output
};

struct CodeSearchResult {
  std::vector<FileSearchResult> matches;
  bool truncated = false;
  std::size_t files_scanned = 0;
  std::string backend_used; // "ripgrep" or "native"
  // Set when the search could not be run at all, for example because the
  // pattern is not a valid regular expression. An error is not an empty
  // result: the caller must report it instead of saying that nothing
  // matched.
  std::string error;
};

// Content search with guardrails. Prefers rg --vimgrep when present, else a
// native recursive walk that skips heavy directories and honors simple
// .gitignore directory entries. Never returns more than max_matches or
// max_bytes; sets truncated when output was cut so callers (and the model)
// know the result is partial.
//
// The pattern is a regular expression in the C++ standard library's default
// grammar (ECMAScript). The two backends do not agree on what that is:
// ripgrep uses the Rust regex crate, which accepts a lone "}" as a literal,
// while the native fallback compiles the pattern with std::regex, which
// rejects it. The pattern is therefore validated once, here, before either
// backend sees it, so both answer the same way: a pattern that does not
// compile is an error, never an empty result.
CodeSearchResult search_code(const SearchCapabilities &caps,
                             const SearchOptions &options);

// File-name discovery. Prefers fd/fdfind when present, else a native walk
// with the same skip rules.
std::vector<std::string> find_files(const SearchCapabilities &caps,
                                    const std::string &directory,
                                    const std::string &glob = "*",
                                    std::size_t max_results = 200);

// Renders the tools section appended to the agent system prompt from what was
// actually detected. No detected tools, no claims.
std::string search_tool_prompt(const SearchCapabilities &caps);

} // namespace Services
