# Product-layer integration audit

Audit date: 2026-10-11. Scope: current worktree at `aa6c72ee`, inspected
without application-code changes. This is a product audit, not a claim that
the source is organised into clean architectural packages.

## Conclusion

**Agentware is partially wired.** Its REPL-to-service paths for local file
operations, project inspection, memory, Git, checkpoints, themes, command
routing, and basic provider dispatch are reachable. The evidence does not
establish live LLM, GitHub, Docker, MCP, or cross-platform operation. MCP is
explicitly simulated, the database is orphaned, and several user-facing
surfaces describe commands or state that do not exist or are not true.

The current build and focused regression suite demonstrate session survival
for many negative paths; they do not demonstrate product completeness.

## Actual layer map

| Logical layer | Actual implementation | Boundary / observation |
|---|---|---|
| Presentation | `main.cpp`, `Agent::run`, `ui.cpp`, direct `cout` in `agent.cpp` | stdin REPL and ANSI stdout; presentation policy is split between `UI` and handlers. |
| Application / orchestration | `Core::Agent` in `agent.cpp` | One large dispatcher branches on `@`, `!`, `/`, colon commands, then chat. It owns memory and lazily creates `AIService`. |
| Domain / services | static `Services::*`, `MemoryManager`, `Validator` | Direct static calls; no dependency container or common result/error type. |
| Infrastructure | CPR/libcurl, OpenSSL, filesystem, `popen`, Docker CLI, GitHub/LLM HTTP, local Ollama, JSON files | Most failures are flattened to strings; state is relative to the process working directory. |
| Return path | handler-specific strings -> `cout`; selected helpers use `UI::{print_warning,print_success}` | No unified rendering, progress, cancellation, or structured telemetry contract. |

The normal chat vertical slice is: `main` -> configuration/logo ->
`Agent::run` -> mode prompt -> `process_user_input` -> `handle_ai_chat` ->
`AIService::chat` -> `WebService::post_json`/provider -> string -> stdout and
`MemoryManager`. Direct tools follow the same first four stages, then call a
validator and one service. `!` and `cmd:` share `execute_routed_command`:
Docker when detected, otherwise an explicit host-execution confirmation.

## Evidence and verification limits

| Method | Result |
|---|---|
| Source/control-flow tracing | Completed for REPL dispatch and every advertised command family. |
| Build | `cmake --build build --target agentware-tests agentware-agent` succeeded on macOS arm64. |
| Unit test target | `ctest --test-dir build --output-on-failure`: `basic_tests` passed (26.03 s). |
| Scripted REPL probes | `remediation_probes.sh`: 85 pass, 3 fail. All three failures were local HTTP fetch probes: the fixture server was unreachable. The script continues after its start-up wait without failing when that server cannot be reached. A separate line-272 comment-continuation defect also emits `2: command not found` and silently skips the intended bad-spec probe. |
| Live integrations | Not verified: no real model credentials, GitHub token/API contract, Docker daemon/container, MCP server, PostgreSQL, or provider mock compose run was exercised. |
| Platforms | Executed only on macOS arm64. Linux and Windows are source/CI-config inspected only. |

## Cross-layer findings

1. **MCP is reachable but not implemented (P1 product truth).** `/mcp` loads
   configuration and exposes lifecycle, resources, tools, and calls;
   `MCPService` only marks its own PID and returns mock JSON-RPC results.
   Start/stop wording now says simulated, but other output can still look
   like a real empty server. No MCP result can reach a real tool.
2. **Help is not an interface contract (P1 UX/product truth).** `help` lists
   `/save`, `/resume`, `/sessions`, `/checkpoints`, and `!` toggle-shell,
   while dispatch accepts `/chat save|resume|list`, `/checkpoint list`, and
   `!<command>` (bare `!` is usage). It omits several reachable commands.
3. **The UI does not carry runtime state reliably (P1 UX).** Startup says
   `Memory=Active` and `Commands=Available` regardless of writable storage,
   provider availability, or Docker. `/stats` maps only Together/Cerebras
   accurately and calls every other selected mode `Local Ollama`.
4. **Error reporting is disconnected (P2).** `/error` renders an in-memory
   `ErrorService`, but ordinary handler failures print strings and have no
   demonstrated calls to `ErrorService::log_error`; reports therefore do not
   represent the session's actual failures.
5. **Configuration has orphaned/dead wiring (P2).** `DB_*` is advertised in
   `.env.example`, but `DatabaseService` has no product caller. Auth has real
   storage/encryption code but `/auth test` is only a key-presence test.
6. **Command safety is duplicated and non-uniform (P1 security).** UI-level
   validator warnings differ from `CommandService` denylist; `/sandbox run`
   takes a different path. Routing makes absence of Docker explicit, but the
   default sandbox mounts the current workspace read/write. Live Docker
   enforcement was not tested.
7. **State ownership is process-local and CWD-coupled (P2).** memory,
   checkpoints, theme/auth/MCP configuration and sandbox configuration use
   relative `data/` paths. This makes product state depend on launch location
   and makes isolation/testing harder.
8. **Cancellation and partial completion lack a contract (P2).** Confirmation
   exists for warning-class commands and host fallback, but no general
   in-flight cancellation exists. Services inconsistently expose timeout,
   truncation, exit code, or partial-result state as prose strings.

## Dependency wiring and reachability

| Component / dependency | Reachability | Evidence / gap |
|---|---|---|
| AI providers + Ollama | Reachable through natural chat and `@` injection | Selection and payload paths are wired; live behavior unverified. Provider table contains six online providers, README claims nine modes but the current UI only offers these six plus three offline models. |
| Files, multi-file, search, codebase | Reachable through colon commands and `/files` | File access gate is called at REPL entry points; service-level callers remain an interface-risk for future paths. |
| Command + Docker sandbox | Reachable via `!`, `cmd:`, `/sandbox run` | Current routing explicitly reports sandbox/host choice. Two policy implementations remain. |
| Git / GitHub | Reachable | Git runs local processes; GitHub needs token/network. Inline review threads have parser tests but no live API test. |
| Web fetch/search | Reachable | Fetch probe's local-success paths failed in this run; search depends on external DuckDuckGo. |
| Checkpoint / theme / context / memory | Reachable | Local file mutations; failure and restore integrity need integration coverage. |
| MCP | UI-reachable, functionally unavailable | Simulated transport; not a real integration. |
| Auth | UI-reachable, partially truthful | CRUD storage is wired; connectivity and credential-security claims are not verified. |
| Error service | UI-reachable, operationally orphaned | No observed bridge from normal errors to log. |
| Database | Not product-reachable | No `Agent` caller. |

## Required next audit gate before implementation

Do not call the product fully wired until a hermetic test can execute one
success and one meaningful failure for each external boundary (each LLM
provider, Ollama, GitHub, Docker, HTTP fetch, MCP decision, persistence and
restore), and its displayed state is asserted. The feature matrix and UI
workstream supply the acceptance inventory.
