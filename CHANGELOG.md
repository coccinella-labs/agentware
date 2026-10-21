# Changelog

<!-- Unreleased records user-visible changes on main between releases. Move entries into a version section when that version is tagged. -->

## [Unreleased]

* Model-driven tool-call loop: opt-in autonomous agent execution (`AGENTWARE_TOOL_CALL_LOOP=1`) allowing models to propose JSON tool calls that the runtime validates, executes, and feeds back as observations across multiple turns.
* Central `ToolRegistry`: a single source of truth for tool definitions, capability classifications (read-only vs mutating), model-surface dispatch metadata, and interactive consent policies.

### Added

* Model tool-call loop runtime: assistant response parser for JSON tool invocations, turn-bounded execution (`AGENTWARE_TOOL_TURN_CAP`, default 8), observation truncation, and per-tool timeout controls (`AGENTWARE_TOOL_TIMEOUT_SEC`, default 30s).
* Hermetic tool loop golden test harness (`tool_loop_golden`, 131 test cases) registered in `ctest` and backed by a local mock provider (`mock_tool_provider.py`).
* Canonical tool-call parser and registry snapshot verification test (`ToolRegistrySnapshotTest`) ensuring runtime tools match `tool_registry_snapshot.txt` with CRLF normalization across platforms.
* Read-only `git:diff` command for working-tree inspection without mutating repository state.
* Tool registry dump via `AGENTWARE_DUMP_TOOLS=1` under `TEST_MODE=1`.
* Repository code styling tools: `.clang-format` configuration and `yamllint` checks integrated into pre-commit and CI.

### Changed

* Linux and macOS CI jobs now run Google Test (`basic_tests`), routing goldens (`routing_golden`), and tool loop goldens (`tool_loop_golden`) after `make preflight`.
* Expanded Google Test suite from 85 cases across 31 suites to 130 cases across 42 suites, adding coverage for `ToolRegistry`, `ToolRunGate`, snapshot parity, and timeout utilities.
* Shortened default network timeout to 10 seconds during tool-loop turns (`AGENTWARE_NET_TIMEOUT_SEC`), keeping a 30-second default for direct user requests.
* Web service loopback targets now bypass ambient HTTP proxies, ensuring local Ollama instances are not routed through external proxies.
* Refined terminal presentation for the engineering loop with Unicode box frames, branded dividers, status spinners, and distinct turn glyphs.
* Windows console now initializes UTF-8 mode and Virtual Terminal processing on startup, and MSVC builds use `/utf-8` to ensure glyphs compile under `/W4 /WX`.

### Fixed

* The daily maintenance workflow publishes its report as a pull request instead of pushing straight to `main`. The `main-merge-queue` ruleset requires three status checks on `main`, and a direct push carries none, so every scheduled run failed with `3 of 3 required status checks are expected` from 2026-10-17 until this change.
* DuckDuckGo search endpoint now follows HTTP redirects and requests JSON results properly.
* Grep match count now accurately reflects filtered results rather than raw scanner totals.
* Made `/proc` symlink verification portable across Linux and macOS in the tool loop golden harness.
* Restored release build reliability on Windows by bootstrapping the `vcpkg` toolchain and aligning macOS release dependencies with CI.
* Eliminated trailing newlines and normalized line endings across maintenance reports and documentation captures.

### Security

* Read-only tool execution gate (`ToolRunGate`): mutating tools (`cmd:`, `write:`, `replace:`, `remember:`, `clear`, `forget`) and heavy tools (`todos:`, `analyze:`, `git:diff`) are strictly rejected when invoked from the model surface.
* Enforced working directory containment on tool arguments: directory traversal (`..`), absolute paths, and external symlinks are refused with `Path escapes the permitted root`.
* Secrets filtering (`is_secrets_path`): sensitive paths (`.env`, `.ssh`, tokens, secret keys) are blocked from reads and filtered out of search, grep, and walker results.
* Interactive consent gate: outbound network tools (`fetch:`, `github:`) require interactive TTY confirmation and fail with `consent denied: no TTY` in unattended or non-interactive environments.

## [0.1.7] - 2026-10-16

Everything below shipped on `main` since 0.1.6 and is tagged here. The first two entries are this week's merges; the rest accumulated under `Unreleased` and moves in with them.

* Terminal layout is width-aware: help, status, prompt, and quick-help output are laid out against the detected terminal width, falling back to 80 columns when unavailable. `ThemeService` now defers to the shared UI color policy.
* Optional Vim-style modal editing for the REPL prompt, disabled by default (`AGENTWARE_VIM_MODE=1` to enable). Piped and redirected stdin keep plain line input.

### Added

* Search environment module: discover `rg`, `fd`/`fdfind`, and `ast-grep`; use a native fallback when they are unavailable; enforce truncation limits; and inject dynamic search prompts.
* Read-only GitHub review-thread search as `github:threads:owner/repo:pr[:query]` without the GraphQL API.
* Daily maintenance workflow with dated reports under `docs/maintenance/`.
* Remediation probe harness wired into `ctest` for known-bad input and failure behavior.

### Changed

* Corrected command dispatch so `clear`, `forget`, and `memory` match only the complete input, and so `!`, `/`, and colon commands are evaluated before `@` file references. Prose such as `clear: the screen please` no longer wipes memory, and `!echo hello@world` reaches the shell instead of being diverted to chat.
* `write:` now asks for confirmation before replacing a file that already exists. Creating a new file is unchanged.
* Shell commands run on the host without isolation now show their output, which was previously discarded.
* Docker availability is checked against the daemon rather than the CLI binary, and the check is bounded to five seconds so a wedged daemon cannot block the agent. On timeout the command's whole process group is cleaned up.
* Windows CI now builds and runs `basic_tests` instead of masking failures with `|| true`; `remediation_probes` remains POSIX-only and is excluded there.
* Renamed the repository from llamaware to agentware and updated ownership, contact, organization references, README, thumbnails, and site branding.
* Routed shell commands through the Docker sandbox with explicit fallback; bounded execution and stopped runs instead of relying on a host shell spawn.
* Enforced file-access policy on all reading sinks; hardened file-command input handling; rejected empty replace patterns instead of hanging.
* Validated git paths without self-blocking on operators; covered remaining git command construction sites.
* Made GitHub integration survive API failures and tolerate null issue fields; reported component errors instead of throwing.
* Aligned help, tool listings, startup state, provider state, and bare `forget`, `memory`, `clear`, and meta-verb routes with actual REPL behavior.
* Named checkpoint IDs explicitly in failures and flows; honored `NO_COLOR` and TTY behavior for color and spinner output.
* Improved release handling: preferred `RELEASE_TOKEN`, allowed manual backfill, filled notes from merged PRs, fixed doubled version prefixes, and switched release notes to the commit log so direct pushes are included.
* Updated user-facing docs and site brief: concise README, operational brief, primary reference links, SECURITY scope, required workflow secrets, UI capture audit, modernization proposal, and completed CHANGELOG entries.
* Tolerated modern lcov 2.x and clang diagnostics in the coverage target.

### Fixed

* Satisfied MSVC by including only complete types in tests.
* Fixed root hangs and missing lifecycle/integration guards in file, memory, and agent flows.
* Built the REPL before coverage probes and corrected remediation probe failure diagnostics.

### Security

* API keys supplied at runtime with `/auth key` are redacted before they reach session memory, logs, or the checkpoint copies of memory. This covers the `/auth key` value only: secrets typed into `!` or `cmd:` shell commands are still recorded verbatim in session memory, so do not treat shell command history as protected.
* Hardened sandbox construction and validation.
* Enforced file-access boundaries and input validation across file, search, shell, git, and GitHub paths.

## [0.1.6] - 2026-05-25

* **CI** - Bumped trivy-action to v0.36.0
* **Docs** - Quieted README and simplified the contributing guide for collaborators
* **Release** - Version bump to 0.1.6

## [0.1.5] - 2026-02-18

* **CI** - Added changelog update step to version.sh
* **Release** - Version bump to 0.1.5

## [0.1.1] - 2026-02-18

* **Security** - Excluded third-party deps from CodeQL to eliminate false positive DES alerts
* **CI/CD** - Added build dependencies (libxattr-dev, libssl-dev, linux-libc-dev) for feature detection
* **Workflows** - Added health checks, TODO management, and milestone assignment for PRs
* **Workflow Fix** - Added locked issue check before commenting to prevent failures
* **Build** - Fixed invalid macOS deployment target in CMake
* **AI** - Resolved Ollama API timeout using std::chrono::seconds
* **Code Quality** - C++20 modernization, clang-tidy fixes, naming conventions
* **Dependencies** - Updated actions/checkout, codeql-action, trivy-action to latest versions
* **Maintenance** - License year update, various dependency bumps

## [0.1] - 2025-09-21

* Initial release with E2E testing, Docker mocks, and CI/CD setup
