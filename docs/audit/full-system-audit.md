# Full-system and UI completeness audit

Audit-first record at `main` (`c75b5557`), macOS 27.0.1. No code changed,
nothing pushed, no history rewritten. Includes the pushed remediation
series and `github:threads` as baseline. Line references verified against
this tree; older `docs/audit/*.md` files describe pre-remediation code
and are history, not spec.

Verification actually run: full GTest (64/64), `ctest` (2/2), `make
preflight-ci` (0 errors, 1 pre-existing warning), plus live REPL probes
for the crash-class findings (exit codes and hangs observed, temp dirs
only, no stray artifacts). Windows/Linux behavior is code-read only.

## 1. Architecture and layer inventory

Single-binary C++20 terminal agent. `src/main.cpp:11-33` loads `.env`,
prints the logo, runs `Core::Agent::run()`, catches to `Fatal error`
exit 1. `Agent` (~2250-line orchestrator) dispatches by prefix in
`process_user_input` (`src/agent.cpp:272-300`): `@` anywhere →
injection, leading `!` → shell, leading `/` → meta, any `:` → direct,
else AI chat. Static stateless services plus owned `MemoryManager` and
lazy `AIService`. Providers: 6 online + 3 Ollama (`src/agent.cpp:99-113,
153-159`, `src/ai_service.cpp:32-93`). Persistence under `data/` plus
`~/.agentware/`. Full maps live in `docs/audit/architecture.md`.

## 2. User-journey trace matrix

`Works` = traced to working code; `Live` = additionally executed here.
`Dies`/`Hangs`/`Fails` are live observations.

| Journey | Path (entry) | Verdict |
|---|---|---|
| Online chat, valid key | `handle_ai_chat` (`:731`) → provider POST | Works (code); unreachable-service yields printed `Error:` string saved as conversation |
| Online chat, missing key | `initialize_mode` throw (`:187`) → `main` catch | Works but hostile: fatal exit 1, no retry |
| Offline chat, Ollama up/down | same path, `localhost:11434` | Works / degrades to printed error string |
| `@file` valid/missing/traversal | `process_file_injections` (`:885`) → `read_file_or_directory` (`:936`) | Works; missing → marker; traversal → `[Access denied]` marker forwarded into AI context |
| `read:` valid/missing/malformed/refused | `:338` + `try_parse_int` + access gate | Works; empty file prints nothing (confusing, live) |
| `write:` valid/traversal | `:394` + gate before writability side effect | Works; creates parents silently; overwrites without confirm |
| `replace:` valid/mismatch/bad count | `:413` + gate + strict parse | Works except empty `old_text` **hangs forever** (Live, §6 F-02) |
| `grep:` valid/empty/bad regex | `:453` + `search_code` (rg or native) | Works; invalid regex → misleading `No matches found` |
| `todos:/analyze:/tree:` | `:513-583` + gates | Work; `components:` on bad path **kills session** (Live, §6 F-01) |
| `/files`, `/fetch` 3 formats | `:1278`, `:1351` | Work, incl. truncation markers and AI-summary fallback |
| `git:status` | `:564` | Works |
| `git:log` | `:562` + `git_service.cpp:26` | **Always fails**: service's own `&&` trips the warning gate (Live, §6 F-03) |
| `github:repo:` | `:596` | Works (anonymous allowed, rate-limited) |
| `github:issues:/health:` | `:604-628`, throwing getters | API failure **kills session** via uncaught throw (code-verified) |
| `github:threads:` | `:629` + try/catch + `get_review_threads` | Works; usage/API-failure/empty distinguished |
| `!cmd` / `cmd:` docker up | `execute_routed_command` (`:781`) → sandbox | Works (fake-daemon tests); live daemon untested here |
| `!cmd` / `cmd:` no docker | same, `y` → host, `N`/EOF → cancel | Works (probes); default is cancel |
| `!cmd` daemon down | same | Works: daemon stderr + exit code, no fallback (Live) |
| `/sandbox run/list/status` | `:2096` direct `execute_command` | Works; fails closed, no host opt-in (by design) |
| `search:` | `:305` DuckDuckGo, no key | Works; failure/empty strings printed |
| `remember:/forget/memory/clear` | `:499-512` prefix match | `remember:` works (empty accepted); bare `forget/memory/clear` **never dispatch** — no colon routes to AI chat (Live, §6 F-04) |
| `/compress` short/long/no-chat | `:1181` + guard | Works; short → message; no-chat → guarded message |
| Checkpoints create/list/restore | `:1438` | Works |
| `/stats /context /mcp /theme /auth /error` | various | Work as displayed, with truth gaps (§4) |
| Cancellations (`N`/EOF) | confirm prompts | Cancel safely everywhere probed |
| Timeouts | 5s host / 5s search / 10s fetch / 30s AI / 120-600s sandbox + stop | Work; distinct messages except web (generic error strings) |

## 3. UI/command completeness matrix

Parser accepts 17 direct forms, 20+ meta branches, `!`, `@`. Help
coverage: `UI::print_help` documents roughly half the direct set and
fictional meta forms; `/help` documents meta plus three `/github`
entries that do not parse; `/tools` documents direct forms with
renamed arguments. Full per-command table with evidence is retained
from the trace; headline mismatches:

- `UI::print_help:81-86` `/save /resume /sessions` — parser has only
  `/chat save|resume|list`. `print_help:113-118` `/checkpoint <name>
  /restore <name> /checkpoints` — parser needs `create|list|info|
  delete|cleanup|restore` and `/restore [id]`.
- `/help` lists `/github repo:|issues:|health:` — no `/github` branch
  exists; all yield `Unknown meta command`. Real form is `github:`.
- `print_help:157` still says `!` = `Toggle shell` (executes; toggle
  was removed). `print_help:64-67` `@file/@directory` keywords do not
  exist; syntax is `@<path>`.
- Undocumented-but-live: `analyze: components: todos: tree: git:
  github:` (in main help), `/theme current`, `/auth status|test`,
  most `/mcp` and `/sandbox` subcommands, `read:<f>:<start>`,
  `replace:…[:count]`, `github:threads:` (except `/tools`).
- Double `Ready - …` lines (`src/ui.cpp:297,299`); `quit` vs `exit`
  split across duplicate quick-help lines; `Goodbye` vs `Goodbye!`.

## 4. Integration and configuration matrix

| Key / integration | Status |
|---|---|
| 6 provider `*_API_KEY` | Read (`src/agent.cpp:154,186`); missing online key = fatal startup |
| `GITHUB_TOKEN` | Read (`src/github_service.cpp:14`); absent = anonymous rate-limited + `Key present` wording absent |
| `SERPAPI_KEY` | Effectively dead: read once, never required/sent; documented reserved |
| `DB_HOST/PORT/NAME/USER/PASSWORD` | Dead: zero code references; `DatabaseService` has zero callers |
| `TEST_*_URL/TEST_MODE`, `HOME`, `PATH` | Read (tests, memory, capability scan) |
| `MCPService` transport | Simulated only, honestly labeled at runtime |
| `AuthService` verify/secure/keygen/recovery, `ErrorService` recovery/config, checkpoint export/import, memory search/facts APIs, `FileService::search_in_directory`, `is_safe_path`, `Validator::validate_file_path`, `WebService::is_available` | Dead API (zero callers, grep-verified) |
| `error_log` | Never flushed (`log_error` zero callers); `/error` always empty |
| `remember:` empty, `write:` empty content, `execute("")` | Accepted silently (no validation) |

## 5. Security-boundary findings

- Routing: `!`/`cmd:` → sandbox when Docker present, deny-by-default
  otherwise, distinct failure reporting (verified live + probes).
- Docker argv: no host shell on any platform (verified by construction
  + argv-equality tests); `--cap-drop ALL`, `no-new-privileges`,
  `--pids-limit 256`, `--network=none` default, workspace-only mounts,
  enforced timeouts with `docker stop`.
- Residuals (accepted, documented): TOCTOU on validated paths;
  root-in-container (no `--user`/`--read-only`); mutable image tags;
  `git_service` `cd "<path>"` quote-breakout shape but path hardcoded
  to `.` (not currently reachable); web SSRF controls weak (scheme +
  dot check, redirects followed); no output encoding anywhere (ANSI
  terminal interpretation); LLM/memory/file/GitHub content rendered
  verbatim and re-fed to the model (prompt injection unmitigated by
  design).

## 6. Findings (P0: none)

No P0. Nothing found permits silent privilege escalation, silent data
destruction, or credential exfiltration beyond the documented,
user-confirmed host-execution path. The strongest candidates were
checked and do not qualify: git shell paths are hardcoded, sandbox
escapes require config-file writes the threat model already grants,
and unsandboxed execution requires explicit opt-in.

**P1 — crashes, hangs, broken capabilities:**

- **F-01 `components:` on bad path kills the session.** Uncaught
  `filesystem_error` from `directory_iterator` (`src/codebase_service.
  cpp:59`) → `Fatal error`, exit 1. Reproduced live
  (`components:/nonexistent_xyz_123`, exit 1). Siblings: `analyze:` uses
  catching tree-walk (safe); `tree:`/`todos:` handle missing paths.
- **F-02 `replace:` with empty `old_text` hangs forever.**
  `find("",pos)` never advances (`src/file_service.cpp:133-138`).
  Reproduced live (killed after 12s, file untouched, CPU spinning).
- **F-03 `git:log` always fails.** Service builds `cd … && git …`;
  the `&&` always trips `validate_command_safe` warnings treated as
  failure (`src/git_service.cpp:26-29`). Reproduced live. `git:analyze`
  embeds the same failure.
- **F-04 bare `forget`/`memory`/`clear` never dispatch.** No colon →
  AI chat (`src/agent.cpp:295-299`); help advertises them as commands.
  Reproduced live (`forget` → Ollama error, memory intact).
- **F-05 `github:issues:`/`health:` API failures kill the session.**
  Throwing getters (`src/github_service.cpp:82-85,190,264,311`) with no
  try in the handler (`src/agent.cpp:608-625`). Code-verified (only
  `threads:` catches). Not executed live (needs token/network
  failure); same unwind shape as F-01.

**P2 — misleading or missing functionality:**

- F-06 `/stats` collapses 4 providers + all Ollama models into `Local
  Ollama` (`src/agent.cpp:1241-1244`); `Memory=Active`,
  `Commands=Available` hardcoded (`src/ui.cpp:287-288`).
- F-07 `print_enterprise_status` prints `16 features active` with zero
  runtime checks and zero callers (`src/ui.cpp:175-207`).
- F-08 Dead UI: `prompt_user`, `print_info`, duration `spinner`
  overload — no callers.
- F-09 Help fiction batch (§3): `/save|/resume|/sessions`,
  `/checkpoint <name>`, `/checkpoints`, `/github …` in `/help`,
  `Toggle shell`, `@file/@directory`, missing subcommand coverage.
- F-10 `/error` permanently empty (`log_error` unwired).
- F-11 Invalid regex → `No matches found` instead of an error
  (`src/file_service.cpp:222-224` swallow).
- F-12 `@` failure markers forwarded into AI context as content.
- F-13 `HEALTHCHECK --version` asserts nothing (`main` takes no args).
- F-14 `remember:`/empty, `write:` empty content accepted silently.
- F-15 `prepare-release.sh` greps a renamed workflow (`name: CI/CD
  Pipeline` vs `CI`); always fails.
- F-16 `DB_*`/SERPAPI dead config; `signed.json` is an allowlist, not
  signing; tripled release paths (known, deferred).

**P3 — inconsistencies:** `Goodbye`/`Goodbye!`, leading-space success
strings, `quit` vs `exit` help split, `an AGENTWARE.md` typo,
`/error export` default vs usage, `cmd:` cancel recorded vs `!`
cancel not, `Local Ollama` label drift, `Memory=Active` siblings above
are P2; empty-file reads printing nothing; `create_*` CPack quirks.

## 7. Test coverage gaps and platform status

Ran here (macOS 27.0.1): 64/64 GTest, 2/2 ctest, `preflight-ci` 0
errors. Covered: parsing, policy incl. symlinks, sandbox argv/spawn/
bounded/fake-daemon, threads incl. pagination, MCP/auth honesty.
Not covered: F-01/F-02/F-05 crash regressions (no tests), help↔parser
consistency (no test), live daemon/Windows/Linux runs (code-read
only), E2E mock suite state (not re-run), release/packaging paths.

## 8. Evidence index

Live reproductions: F-01 exit 1 + `Fatal error: filesystem error…`;
F-02 hang killed at 12s; F-03 `Error: Command validation failed`;
F-04 Ollama-error-not-cleared; all in temp dirs, session logs
retained in `/tmp/a1|a2.log` style probe outputs during the audit.
Static evidence carries file:line throughout §§2-5.
