# Architecture

Reconstructed from the code at `main` (`7300b4e3`), not from documentation.
`VERIFIED` means read directly during this audit. `INFERRED` means a
reasonable reading that was not traced end to end.

## Purpose and primary use cases

Agentware is a single-binary terminal AI coding and operations agent written
in C++20. The entry point (`src/main.cpp:11-25`) loads `.env`, prints a logo,
and starts a read-eval-print loop (`Core::Agent::run()`).

Verified use cases, from `src/agent.cpp:237-265,267-533,611-659` and
`src/ai_service.cpp:50-94,108-211`:

- Chat with an LLM, optionally grounded in local files via `@` injection.
- Direct tools via `prefix:args`: `search:`, `cmd:`, `read:`, `write:`,
  `replace:`, `grep:`, `remember:`, `analyze:`, `git:`, `github:`.
- Shell passthrough via `!command` with a confirmation prompt.
- Meta commands via `/name`: `/chat`, `/tools`, `/memory`, `/compress`,
  `/stats`, `/context`, `/files`, `/fetch`, `/checkpoint`, `/restore`,
  `/mcp`, `/theme`, `/auth`, `/sandbox`, `/error`, `/help`, `/clear`, `/quit`.
- Online mode through six OpenAI-compatible providers (Together, Cerebras,
  Fireworks, Groq, DeepSeek, OpenAI) and offline mode through local Ollama
  (`http://localhost:11434/api/chat`).

## Components and responsibilities

| Component | Location | Responsibility |
|---|---|---|
| `main` | `src/main.cpp:11-36` | Load env, print logo, run agent, catch `std::exception` to exit 1 |
| `Core::Agent` | `src/agent.cpp:40-2009`, `src/include/agent.h:18-85` | REPL orchestrator, mode selection, input dispatch, all `handle_*` |
| `Services::AIService` | `src/ai_service.cpp:29-379` | Provider URLs, payloads, `chat()`, Cerebras SSE parsing |
| `Data::MemoryManager` | `src/memory_manager.cpp:18-814` | Append-only `data/memory.txt`, last-20 context, LRU cache, global facts, compression |
| `Utils::Config` | `src/config.cpp:23-75` | Minimal `.env` parser, `setenv` per `KEY=VALUE` |
| `Utils::UI` | `src/ui.cpp:31-303` | Logo, help, ready screen, spinner, colored status output |
| `Utils::Validator` | `src/validation.cpp:10-293` | File/command/query checks returning `{is_valid, error, warnings}` |
| `Services::CommandService` | `src/command_service.cpp:14-81` | Shell exec with denylist, 5s timeout, 10KB truncation |
| `Services::FileService` | `src/file_service.cpp:14-344` | Read/range/write/replace/search with size caps |
| `Services::CodebaseService` | `src/codebase_service.cpp:12-233` | Structure analysis, TODO scan, directory tree, file-type stats |
| `Services::GitService` | `src/git_service.cpp:13-148` | `git log/status` via `popen`, repo analysis |
| `Services::GitHubService` | `src/github_service.cpp:13-430` | Issues, PRs, milestones, labels, webhooks, health check via REST |
| `Services::WebService` | `src/web_service.cpp:37-523` | Fetch URL/text/JSON, POST JSON, search (see gap G-04) |
| `Services::ContextService` | `src/context_service.cpp:15-348` | Hierarchical `AGENTWARE.md` discovery, merge, cache |
| `Services::MultiFileService` | `src/multi_file_service.cpp:1-598` | Multi-file and directory reads with limits and gitignore support |
| `Services::CheckpointService` | `src/checkpoint_service.cpp:14-465` | File-tree snapshots plus memory into `metadata.json`, restore |
| `Services::MCPService` | `src/mcp_service.cpp:20-481` | Server registry and config; transport is simulated (see gap G-05) |
| `Services::AuthService` | `src/auth_service.cpp:30-785` | Provider key store with AES-256-GCM via OpenSSL EVP |
| `Services::SandboxService` | `src/sandbox_service.cpp:28-678` | `docker run` based command isolation with memory/CPU/network limits |
| `Services::ThemeService` | `src/theme_service.cpp:13-429` | Four built-in color themes with file persistence |
| `Services::ErrorService` | `src/error_service.cpp:12-438` | In-memory error log, validation rules, report/export |
| `agentware::DatabaseService` | `src/database_service.cpp:7-148` | PostgreSQL access, compiled out without `HAVE_PQXX`; no callers |
| `Utils::Platform` | `src/include/platform.h:1-56` | `popen`/`_popen`, null-device and redirect abstraction |
| `Version` | `src/version.cpp:1-20`, `src/include/version.h.in` | Version string from CMake `configure_file` |

Namespace note (VERIFIED): most services live in `Services::`, the agent in
`Core::`, memory in `Data::`, utilities in `Utils::`, but the database
service uses a divergent `agentware::` namespace
(`src/include/database_service.h:28`).

## Execution flow, traced from entry to completion

One full cycle for input `Hello world` (VERIFIED by reading
`src/main.cpp`, `src/agent.cpp:41-107,109-235`, `src/ai_service.cpp`):

1. `main.cpp:14` calls `Config::load_environment(".env")`, a silent no-op
   when no `.env` exists (`src/config.cpp:23-65`).
2. `main.cpp:19` prints the logo (`src/ui.cpp:31`).
3. `main.cpp:22-25` constructs `Core::Agent` (creates `MemoryManager`,
   leaves `ai_service_` null) and calls `run()`.
4. `run()` calls `initialize_mode()` (`src/agent.cpp:109-196`): prompts
   `Mode [1=Online/2=Offline]`, then a provider table (online, requires the
   matching `*_API_KEY` env var or throws) or a model table (offline).
5. `run()` prints the ready interface and enters `while(true)`:
   `cout << "> "`, `getline`, trim, `exit`/`quit` breaks, `help` and
   `version` are handled inline, everything else goes to
   `process_user_input()`.
6. `Hello world` matches no prefix, so `handle_ai_chat()` lazily creates
   `AIService(mode, key)`, starts a spinner thread, merges
   `memory_->get_context_string()` with
   `ContextService::load_hierarchical_context(".")`, and calls
   `AIService::chat(input, full_context)`.
7. `chat()` builds the provider payload, posts through
   `WebService::post_json` (CPR/libcurl), parses
   `choices[0].message.content` (or the Cerebras SSE stream, or Ollama
   `message.content`), and returns a string.
8. The spinner joins, the response prints, and
   `memory_->save_interaction(input, response)` appends
   `User:/Assistant:/---` to `data/memory.txt`. The loop repeats.

Data-flow diagram:

```
stdin --> Agent::run --> process_user_input --+--> handle_ai_chat --> AIService --> WebService --> provider HTTPS
                                              |       ^  v                                   |
                                              |   MemoryManager <-- data/memory.txt           v
                                              +--> handle_direct_command --> Validator --> File/Command/Codebase/Git/GitHub/WebService
                                              +--> handle_shell_command --> CommandService (popen)
                                              +--> handle_meta_command --> Checkpoint/MCP/Theme/Auth/Sandbox/Error/ContextService
stdout <-- UI (colors, spinner, tables) -------+
```

`Agent` is the sole orchestrator. Every service except `MemoryManager`
(owned instance) and `AIService` (lazy instance) is a static stateless
class. There is no event bus, no background task, and no inter-service
messaging beyond these direct calls.

## Public APIs, internal interfaces, extension points

- The public API is the stdin/stdout REPL. There is no socket, REST, or FFI
  surface. `main()` takes no arguments (VERIFIED `src/main.cpp:11`).
- Internal interfaces are the `Services::*` static classes, `Data::`
  memory, and `Utils::` helpers, behind the `AGENTWARE_API` visibility
  macro (`src/include/config.h`, `CMakeLists.txt:436-459`).
- Extension follows fixed patterns (VERIFIED): a new LLM needs an
  `AgentMode` entry plus branches in `is_online_mode`, `get_api_url`,
  `create_payload`, `chat`, the `model_names` map, and the provider table;
  a new `:` tool needs a branch in `handle_direct_command` plus help text;
  a new `/` tool needs a branch in `handle_meta_command` plus a `handle_*`
  method; MCP servers, themes, and sandboxes each have a register/create
  function; dropping `AGENTWARE.md` files extends context.

## Dependencies, integrations, runtime

Build (VERIFIED `CMakeLists.txt:34-360`): CMake 3.14+, C++20, Threads,
`CURL REQUIRED`, OpenSSL, zlib, `nlohmann_json 3.11.2`, CPR 1.10.5, GTest
1.14.0, optional `libpqxx` (absence only warns and compiles a stub).

External services: six cloud LLM HTTPS endpoints, local Ollama at
`localhost:11434`, DuckDuckGo Instant Answer for search, arbitrary HTTP(S)
fetch, GitHub REST with `GITHUB_TOKEN`, a Docker daemon for sandboxes, and
PostgreSQL only when `libpqxx` is present.

Runtime requirements: provider API keys in the environment, a writable
`data/` directory, and optionally `git` and `docker` on PATH.

## Configuration, errors, observability

- Configuration is `.env` parsing only, with no schema validation and no
  reload (`src/config.cpp:23-75`). `.env.example` documents 12 keys.
- Errors are returned as `"Error: ..."` strings, printed, and usually saved
  to memory. `Validator` produces warnings plus occasional `y/N`
  confirmations. `CommandService` blocks a regex denylist with a 5s timeout.
  Only `main()` and a few handlers use `try/catch`.
- Observability is the colored UI, a spinner thread, `/stats` session
  counters, `ErrorService` log/report/export, checkpoint metadata, and CI
  smoke checks. There is no log framework or metrics sink.

## Security boundaries and trust

The trusted party is the local interactive user. Remote LLM, web, GitHub,
and MCP content is untrusted but rendered without sanitizing. Mitigations
are partial by design: substring warnings plus a regex command denylist,
5s/10KB execution bounds, file size caps, AES-256-GCM credential storage,
optional `--network=none` sandboxes, and fail-closed behavior when Docker
is absent. Known holes are recorded as gaps G-01 (unvalidated paths),
G-02 (unguarded parsing crashes), and G-06 (shell-concatenated git and
volume pass-through) in `docs/audit/gaps-and-risks.md`.

## Build, test, package, release

- CMake builds `agentware_lib`, the `agentware-agent` binary into
  `build/bin/`, and `agentware-tests`; CPack declares DEB/RPM/TGZ,
  DragNDrop, NSIS/ZIP (`CMakeLists.txt:421-563`).
- Make wraps configure, smoke tests, E2E, packaging, install to
  `/usr/local/bin`, Docker build, and preflight (`Makefile:1-171`).
- Docker is a two-stage `ubuntu:26.04` build with a venv for Python test
  deps, a non-root `agentware` user, and `CMD agentware-agent`
  (`Dockerfile:1-117`).
- E2E composes seven `nginx:alpine` provider mocks plus a builder-image
  test runner driven by an `expect` script (`docker-compose.e2e.yml`,
  `src/tests/`).
- Release is tag-driven (`v*` via `release.yml`), with version bumps from
  PR labels (`version-bump.yml`) and a `VERSION`-diff fallback job in
  `ci.yml`.
