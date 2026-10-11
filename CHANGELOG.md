# Changelog

<!-- Unreleased records user-visible changes on main between releases. Move entries into a version section when that version is tagged. -->

## [Unreleased]

### Added

* Search environment module: discover `rg`, `fd`/`fdfind`, and `ast-grep`; use a native fallback when they are unavailable; enforce truncation limits; and inject dynamic search prompts.
* Read-only GitHub review-thread search as `github:threads:owner/repo:pr[:query]` without the GraphQL API.
* Daily maintenance workflow with dated reports under `docs/maintenance/`.
* Remediation probe harness wired into `ctest` for known-bad input and failure behavior.

### Changed

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
