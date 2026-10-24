# Changelog

<!-- Unreleased records user-visible changes on main between releases. Move entries into a version section when that version is tagged. -->

## [Unreleased]

* Model-driven tool-call loop: opt-in autonomous agent execution (`AGENTWARE_TOOL_CALL_LOOP=1`) allowing models to propose JSON tool calls that the runtime validates, executes, and feeds back as observations across multiple turns.
* Central `ToolRegistry`: a single source of truth for tool definitions, capability classifications (read-only vs mutating), model-surface dispatch metadata, and interactive consent policies.
* Optional xoi Ollama adapter: with `-DAGENTWARE_USE_XOI_OLLAMA=ON`, CMake fetches a public, pinned xoi commit and local Ollama requests route through it. Off by default. Agentware keeps its prompt, text-based tool loop, permissions, and safety checks; the existing request timeout is preserved, xoi retries are disabled so Agentware owns the loop retry budget, and proxies are bypassed for local Ollama. Experimental first-provider integration, not a production-readiness claim (#248, #249).

### Added

* Model tool-call loop runtime: assistant response parser for JSON tool invocations, turn-bounded execution (`AGENTWARE_TOOL_TURN_CAP`, default 8), observation truncation, and per-tool timeout controls (`AGENTWARE_TOOL_TIMEOUT_SEC`, default 30s).
* Tool loop reliability (#245): model-driven retries are bounded across a shared budget, a pure model timeout recovers instead of ending the session, and argument errors from `read:` are taught back to the model as observations.
* Observation framing for the tool loop (#241, #246): tool results are sent as a named `Result of <tool>` block with an instruction to answer from it, explicitly marked untrusted data, not instructions.
* Registry-rendered model tool contract (#241): the system prompt for the loop is generated from the registry and the model-surface policy, naming exactly the model-callable tools with their JSON argument names, a worked example, and the refusal line. The completion signal accepts `answer` (the older `summary` key still works), and the bare `memory` verb maps to its dispatch form. With the loop disabled the main-tip prompt is byte-identical to the previous hand-written one, pinned by a golden test.
* CLI behavior test wired into `ctest` on Linux, macOS, and Windows, asserting the binary's `help` and `version` behavior and the `write:`/`read` round trip (#225).
* Provider readiness test target: the mock-server wait asserts prompt readiness, that the server does not resolve its own bind address, that a bind error is still a bind error, and that a provider which genuinely fails to start fails fast with a reason instead of spending a deadline in silence (#238).
* Maintenance report generator now has a regression test pinning its shape (#243).
* Hermetic tool loop golden test harness (`tool_loop_golden`, 131 test cases) registered in `ctest` and backed by a local mock provider (`mock_tool_provider.py`).
* Canonical tool-call parser and registry snapshot verification test (`ToolRegistrySnapshotTest`) ensuring runtime tools match `tool_registry_snapshot.txt` with CRLF normalization across platforms.
* Read-only `git:diff` command for working-tree inspection without mutating repository state.
* Tool registry dump via `AGENTWARE_DUMP_TOOLS=1` under `TEST_MODE=1`.
* Repository code styling tools: `.clang-format` configuration and `yamllint` checks integrated into pre-commit and CI.
* `AGENTWARE_SEARCH_BACKEND=native` forces the fallback search backend even on hosts with ripgrep on `PATH`, so both backends are exercised on the same machine and in CI (#230).
* Isolated macOS runner trial workflow under `.github/workflows/`, kept experimental and out of the required checks (#237).

### Changed

* Linux and macOS CI jobs now run Google Test (`basic_tests`), routing goldens (`routing_golden`), and tool loop goldens (`tool_loop_golden`) after `make preflight`.
* CI hardening round (Oct 21-23): per-job timeouts and a pull-request concurrency group (#221), stubbed pull-request linting no longer blocked by a stale third-party apt source (#229), clang-tidy analyzer checks enforced as a gate (#250), the coverage target builds the CLI behavior test binary first (#239), coverage `apt update` network calls are time-bounded (#244), and TODO sub-issue extraction only triggers when an issue is opened (#240).
* `make preflight` is a local developer check and no longer runs inside the CI build jobs (#222, #223).
* Remediation probes are usable and run in CI on Linux (#226); the CLI behavior test builds on Windows and drops its last POSIX-only name (#225).
* The golden harnesses survived hardening for the macOS runner and stay host-independent: the mock provider is timestamped for diagnosis, agent startup is traced with the search backend reported and the harness and agent timed separately, a per-case provider cannot leak, the EXIT trap survives an interrupt, tool loop and routing goldens share one shortened network timeout, and search goldens run the same way on every host (#215).
* Tool-call argument handling now reads scalar types as model output: a string-typed line count like `"0"` or a null path produces an observation instead of killing the loop (#241).
* Shortened default network timeout to 10 seconds during tool-loop turns (`AGENTWARE_NET_TIMEOUT_SEC`), keeping a 30-second default for direct user requests.
* Web service loopback targets now bypass ambient HTTP proxies, ensuring local Ollama instances are not routed through external proxies.
* Refined terminal presentation for the engineering loop with Unicode box frames, branded dividers, status spinners, and distinct turn glyphs.
* Windows console now initializes UTF-8 mode and Virtual Terminal processing on startup, and MSVC builds use `/utf-8` to ensure glyphs compile under `/W4 /WX`.

### Fixed

* The daily maintenance workflow publishes its report as a pull request instead of pushing straight to `main`. The `main-merge-queue` ruleset requires three status checks on `main`, and a direct push carries none, so every scheduled run failed with `3 of 3 required status checks are expected` from 2026-10-17 until this change.
* DuckDuckGo search endpoint now follows HTTP redirects and requests JSON results properly.
* Grep match count now accurately reflects filtered results rather than raw scanner totals.
* Made `/proc` symlink verification portable across Linux and macOS in the tool loop golden harness.
* A search pattern the regex engine rejects is an error, not an empty result (#230). The pattern is validated once against the shared ECMAScript-style grammar before either backend sees it, so a failing compile reports `invalid search pattern` instead of `No matches found`; the ripgrep path also reports patterns its engine rejects rather than returning nothing. The error is identical whether or not the host has ripgrep, and CI runs the grep goldens with the native fallback forced so both backends are covered on every runner.
* Ripgrep's exit status is read the way ripgrep defines it, child exit codes are decoded instead of compared raw, and a search backend that died is treated as a failure too (#230).
* `make test-e2e` reads its binary path again: the Makefile target now sets `BINARY_PATH` to the freshly built binary, fixing a `tcl no such variable` failure on every local run since `run_e2e_tests.sh` began reading the variable (#242, closes #232).
* Xoi adapter follow-ups (#251): the adapter is pinned to the merged xoi commit so local verification matches what CMake fetches, macOS xoi builds and tests address compiler warnings and use Homebrew LLVM, an unreachable AI error fallback was removed, and the new opt-in xoi CI job runs on Windows too, with the POSIX-shell suites excluded there.
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
