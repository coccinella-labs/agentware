# Current state

Inventory, implementation status, and quality findings at `main`
(`7300b4e3`), with evidence. No code was changed for this audit except
building the existing targets to record results.

## Inventory

Repository root holds a C++20 project plus its automation: `src/` (~12.9k
LOC across 24 `.cpp` and 25 headers), `CMakeLists.txt` (617 lines),
`Makefile` (171 lines), `Dockerfile`, `docker-compose.e2e.yml`,
`.env.example`, `VERSION` (`0.1.6`), `CHANGELOG.md`, `CONTRIBUTING.md`,
`LICENSE` (Apache-2.0), `signed.json` (four maintainers), `data/` (MCP and
theme JSON), `docs/index.html` (the only file in `docs/`), and
`.github/` (9 workflows, issue templates, CodeQL config, dependabot,
`SECURITY.md`). There are no JS/Python/Go/Rust manifests and no lockfiles;
C++ dependencies resolve via `find_package` with `FetchContent` fallback.

`src/` entry points: `src/main.cpp` (36 lines) into `Core::Agent::run()`
(`src/agent.cpp`, 2009 lines). Tests live in `src/tests/` (5 files);
scripts in `src/scripts/` (7 files). There are no examples, fixtures,
demos, or benchmarks directories.

Git state: 331 commits on `main`, tags `v0.0.1` through `v0.1.6` (plus two
malformed `v-0.1.2+18`, `v-0.1.3` tags), one remote branch
`origin/version-bump-0.1.7` (unmerged). Recent history (Sep 2026) is
UI polish, rebranding, and CI/Docker fixes; no service feature commits
since Feb 2026.

## Implementation status by subsystem

| Subsystem | Status | Evidence |
|---|---|---|
| REPL loop and dispatch (`:`, `@`, `!`, `/`) | Working | `src/agent.cpp:59-107,237-265,267-659` full loop and all prefixes |
| Online providers (6) and offline Ollama | Working | `src/ai_service.cpp:50-94,261-379`, SSE parser `:214-245` |
| Memory, global facts, compression | Working | `src/memory_manager.cpp:83-141,213-311,412-564` |
| Command exec with denylist/timeout | Working, bounded | `src/command_service.cpp:14-81` |
| File read/write/replace/search | Working | `src/file_service.cpp:14-344` |
| Codebase analysis and TODO scan | Working | `src/codebase_service.cpp:14-200` |
| Git log/status analysis | Working | `src/git_service.cpp:18-148` |
| GitHub issues/PRs/labels/health | Working with token+network | `src/github_service.cpp:49-359` |
| Web fetch and POST | Working | `src/web_service.cpp:230-523` |
| Hierarchical `AGENTWARE.md` context | Working | `src/context_service.cpp:15-337` |
| Multi-file and directory reads | Working | `src/multi_file_service.cpp`, used at `src/agent.cpp:720-732,1039-1101` |
| Checkpoints with restore | Working | `src/checkpoint_service.cpp:106-402` |
| Themes (4 built-in, persisted) | Working | `src/theme_service.cpp:21-414` |
| Docker sandboxes | Working when Docker exists, else graceful error | `src/sandbox_service.cpp:171-262,619-659` |
| Config, UI, validation, version | Working | `src/config.cpp`, `src/ui.cpp`, `src/validation.cpp`, `src/version.cpp` |
| Auth encryption and provider CRUD | Partial | AES-GCM real (`src/auth_service.cpp:267-400`), but `test_provider_connection` only checks non-empty (`:714-729`), `is_credential_secure` returns `true` (`:756`), key generation is a placeholder (`:759-762`) |
| Error log/query/export | Partial | Real (`src/error_service.cpp:108-346`), but recovery registration is a no-op (`:248`), auto-recovery trivial (`:229`), and `return false; // Not implemented` at `:239` |
| MCP servers | Stub | Config and listing real, but `send_mcp_request` returns empty mock arrays (`src/mcp_service.cpp:39-77`), start only sets a flag (`:79-113`), agent prints `started (simulated)` (`src/agent.cpp:1448,1465`) |
| DatabaseService | Absent/disabled | `#ifdef HAVE_PQXX` else `return false/nullptr` (`src/database_service.cpp:16-148`); zero callers found by repo-wide grep |
| Web `search:` | Unclear/mismatched | Gates on `SERPAPI_KEY` but queries DuckDuckGo with the key appended (`src/web_service.cpp:43-67`); SerpAPI never called |
| Session token usage | Dead | Declared (`src/include/agent.h:30`), printed (`src/agent.cpp:1008`), never incremented (repo-wide grep) |
| `should_skip_file`, `safe_get_string` | Dead | Defined (`src/agent.cpp:771`, `src/ai_service.cpp:247`), no call sites (repo-wide grep) |
| `/fetch` text format | Broken | Fetches then `return`s without printing or saving (`src/agent.cpp:1151-1153`) |
| `/compress` before any chat | Broken | Dereferences `ai_service_` unconditionally (`src/agent.cpp:977`); the pointer is only created lazily in `handle_ai_chat` |
| `read:file:non-numeric` | Broken | Unguarded `std::stoi` (`src/agent.cpp:317-322`) throws out to `main`, exiting 1 |
| `--version` flag | Missing | `main()` takes no arguments (`src/main.cpp:11`); the Docker `HEALTHCHECK agentware-agent --version` starts the REPL instead |

## Quality findings with verification results

Commands run during this audit (macOS arm64, Homebrew deps):

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`: configures clean
  (10.8s). FetchContent pulled CPR/GTest as designed.
- `cmake --build build --target agentware-tests`: builds clean (only
  warning is the pre-existing macOS-14 vs Homebrew dylib version skew for
  OpenSSL/zlib).
- `./build/agentware-tests`: **2/2 pass** (`BasicTest.SanityCheck`,
  `VersionTest.VersionCommand`).
- `cmake --build build --target agentware-agent`: builds clean.
- Piped smoke `printf '2\nversion\nhelp\nexit\n'`: runs to `Goodbye`,
  exit 0.
- `--version < /dev/null`: exits 0 **without printing any version**,
  confirming the HEALTHCHECK asserts nothing (it does not hang on closed
  stdin; with an open TTY it would sit at the prompt instead).
- `grep -c TODO|FIXME|HACK -r src`: zero real TODOs; the only hits are the
  detector string literals in `codebase_service.*` themselves.
- Repo-wide grep for `mock_ollama`: no references outside
  `src/tests/mock_ollama.py`, confirming it is orphaned (compose uses
  nginx mocks for all seven providers including Ollama).

Test posture: the only compiled tests are the two trivial GTest asserts
(`src/tests/main_test.cpp:1-16`) for ~12.9k LOC. `make test` and the CMake
`check` target pipe canned input and assert the binary exists and exits.
Behavioral coverage comes solely from the `expect`-driven E2E over nginx
mocks (six cloud providers, substring match only, no negative or schema
cases), which runs only on `main` or manual dispatch (`ci.yml:160`), and
whose preflight copy degrades to a warning. Codecov runs with
`fail_ci_if_error: false` and Windows preflight uses `|| true`.

Dependency posture: fetched C++ deps are pinned (CPR 1.10.5, GTest 1.14.0,
nlohmann 3.11.2, OpenSSL 3.2.1, zlib 1.3) but system-first resolution lets
local and CI builds diverge silently; apt/brew/choco/vcpkg installs are
unpinned and dependabot covers only GitHub Actions and Docker. Two
reproducibility defects are baked in: `__DATE__`/`__TIME__` in
`src/version.cpp:10-11` and version-from-`git describe` with anomalous
tags in the history.

Release posture: three divergent install paths (`make package` dist copy,
`cmake install` binary plus config, `make install` binary-only copy),
overlapping release jobs (`ci.yml` VERSION-diff, `version-bump.yml`,
`release.yml` on `v*`, `security.yml` tarball), CPack runtime deps naming
a `-dev` package, `prepare-release.sh` referencing a renamed workflow
(`name: CI/CD Pipeline` vs current `name: CI`), and `CHANGELOG.md`
placeholders (`0.1.5`/`0.1.6` are bare `*`). `signed.json` is an
allowlist, not cryptographic signing; no SBOM, provenance, or cosign.

Platform posture: CI builds ubuntu/macos release+debug and windows
release, and all pass. Hardcoded Apple `arm64 -march=armv8.4-a+...`
excludes Intel Macs; the pre-commit clang-format path is an absolute
Homebrew path that fails on Linux; clang-tidy is disabled in CMake over
libpqxx while the lint job still advertises it.

Docs posture: `README.md` (34 lines) understates the project as small dev
tools with no usage section; `docs/` holds only the marketing
`index.html`; prior `docs/*.md` guides were deleted in `31f70e34`,
`f2c246f4`, `64c5037b`; `CONTRIBUTING.md` is collaborators-only.
