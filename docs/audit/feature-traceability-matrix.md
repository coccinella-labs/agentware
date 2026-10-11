# Feature traceability matrix

Status: **verified** = source traced plus a passing local probe/test where
applicable; **partial** = path exists but external or key behavior was not
proved; **misleading** = reachable surface differs materially from product
claim; **unreachable** = shipped implementation has no product entry point.

| Capability / UI entry | Dispatch -> orchestration | Backend/dependencies | Return path | Evidence/status |
|---|---|---|---|---|
| Natural chat | unprefixed -> `handle_ai_chat` | `AIService` -> HTTP provider/Ollama; memory/context | response -> stdout + memory | Partial; live model untested |
| `@path prompt` injection | `handle_file_injection_command` | filesystem / `MultiFileService`, then chat | injected prompt -> chat output | Partial; local refusal probes pass, chat untested |
| `read:` / range | direct handler | validator -> `FileService` | result -> stdout + memory | Verified negative/range probes |
| `write:` / `replace:` | direct handler | validator -> `FileService` | string -> stdout + memory | Verified focused probes; mutation rollback/atomicity untested |
| `grep:` | direct handler | validator -> `SearchService` (`rg`/fallback) | formatted matches/truncation -> stdout | Verified local probe; both backends not exercised |
| `analyze:`, `components:`, `todos:`, `tree:` | direct handler | validator -> `CodebaseService` | string -> stdout + memory | Verified failure probes; comprehensive output untested |
| `git:status|log|analyze` | direct handler | `GitService` -> git process | string -> stdout + memory | Partial; log local probe passes |
| `github:repo|issues|health|threads` | direct handler | `GitHubService` -> GitHub REST/token | string -> stdout + memory | Partial; parser/invalid input probes only |
| `search:` | direct handler | `WebService` -> DuckDuckGo | string -> stdout + memory | Partial; external backend untested |
| `cmd:` / `!command` | direct / shell handler -> shared router | validator -> Docker sandbox or confirmed host `CommandService` | backend label + result -> stdout + memory | Verified no-Docker/fake-Docker routing probes; live Docker untested |
| `remember:`, `memory`, `forget`, `clear` | direct handler | `MemoryManager` | string -> stdout | Verified focused probes; durable storage failure only partly covered |
| `/chat save|resume|list` | meta -> chat handler | `MemoryManager` files | stdout | Partial; no restore round-trip E2E |
| `/memory show|add`, `/compress`, `/stats` | meta -> helper | memory / AI for compression | stdout | Partial; no live compression; stats inaccurate for most modes |
| `/context show|refresh|create` | meta -> context handler | `ContextService`, filesystem | stdout | Partial; only no-context probe |
| `/files` | meta -> multi-file handler | validator -> `MultiFileService` | formatted contents -> stdout + memory | Verified access/usage probes; limits and gitignore untested |
| `/fetch` | meta -> fetch handler | `WebService` HTTP, optional AI summary | result -> stdout + memory | Partial; errors pass, local-success fixture failed in this run |
| `/checkpoint`, `/restore` | meta -> checkpoint handler | `CheckpointService`, filesystem/memory | stdout + memory record | Partial; no create/restore integrity E2E |
| `/mcp ...` | meta -> MCP handler | config + simulated `MCPService` | stdout | Misleading/partial: no actual MCP process or transport |
| `/theme ...` | meta -> theme handler | `ThemeService`, JSON/file | stdout | Partial; list probe only |
| `/auth ...` | meta -> auth handler | `AuthService`, OpenSSL/files | stdout | Partial; key-presence wording verified, connection not verified |
| `/sandbox ...` | meta -> sandbox handler | Docker CLI/config | stdout + memory record | Partial; config/list/status reachable, daemon untested |
| `/error ...` | meta -> error handler | `ErrorService` in-memory/export | stdout + memory record | Misleading: normal errors do not populate its log |
| `help`, `/help`, `/tools` | inline / meta / helper | presentation only | stdout | Misleading: command inventory and syntax disagree |
| `version`, `exit`, `quit`, `/clear` | inline / meta | presentation/process loop | stdout/exit | Verified locally; `--version` CLI flag absent |
| Database configuration | none | `DatabaseService`, optional libpqxx | none | Unreachable |

The matrix deliberately groups syntax variants that share a single
implementation. It covers every command advertised by README, `help`,
`/help`, `/tools`, startup quick help, and the REPL's entry prefixes; claims
not represented by an entry are not product capabilities.
