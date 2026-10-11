# Changelog

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
