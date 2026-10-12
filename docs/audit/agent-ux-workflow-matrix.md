# Agentware: UX Workflow Matrix (Pass 1)

Companion to `agent-ux-audit.md`. Audit of commit `4caff2f7`, clean working
tree. Built from source at `4caff2f7`; hermetic runtime harness; no live
provider calls.

## Remediation status

A test-first remediation pass followed the accepted Pass 1 audit. Rows below
that read **Fixed** were changed and are now covered by regression tests; rows
reading **Open** were deliberately deferred. The original statuses are
preserved in the table cells.

| Area | Rows now fixed | Rows still open |
| --- | --- | --- |
| 3. File operations | 3.4 `write:` overwrite now requires consent | 3.6 absolute-path allowance |
| 5. Shell and sandbox | 5.4 `@` hijack, 5.5 Docker detection, 5.8 host output | 5.10 denylist coverage, 5.14 cleanup no-op, 5.15 no cancellation |
| 7. Authentication | 7.3 credential no longer persisted | 7.3 still inert, 7.4 inert, 7.5 misreports provider and model |
| 8. Memory | 8.5 and 8.6 no longer reachable from prose | 8.8 undisclosed recording, 8.9 unbounded growth, 8.10 read window |
| 12. Help and status | 12.11 `/sandbox status` accuracy | 12.8 `/error` inert |
| 1. Startup | n/a | 1.5 fatal exit on missing key, 1.6 no runtime mode switch |

Follow-up backlog, in operational order: bound the Docker availability probe;
define a redaction policy for shell-command history; colon fallback to chat
(which also resolves the mixed-prose regression); inert `/auth set`;
`/auth status` accuracy; `/error` wiring; HTTP status and rate limits; and the
Windows CI job, which masks build failures with `|| true` and never runs
CTest. See the full detail in `agent-ux-audit.md` section 0.

## Status legend

| Status | Meaning |
| --- | --- |
| **Supported** | Implemented, reachable, and behaved correctly when exercised |
| **Partial** | Implemented but incomplete, undisclosed, or misleading |
| **Unsupported** | Advertised or expected, but not implemented |
| **Unverified** | Could not be established in this environment; not asserted |

## Evidence legend

| Tag | Meaning |
| --- | --- |
| **[EXEC]** | Reproduced against the freshly built binary |
| **[SRC]** | Source inspection only |
| **[UNVER]** | Not established; explicitly not asserted |

---

## 1. Startup and session entry

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 1.1 | Logo and banner | `main.cpp:19`, `ui.cpp:291` | **Supported** | [EXEC] | Banner prints. `Memory=Active` and `Commands=Available` are hardcoded, not probed (P2-14) |
| 1.2 | Mode selection | `agent.cpp:162` | **Supported** | [EXEC] | `Mode [1=Online / 2=Offline] (default 2):`. Empty input and EOF take the default; invalid input loops with a warning |
| 1.3 | Provider selection (online) | `agent.cpp:167` | **Partial** | [EXEC] | Works, but the registry disagrees with `/auth` (P2-5) |
| 1.4 | Model selection (offline) | `agent.cpp:208` | **Supported** | [EXEC] | No Ollama reachability check |
| 1.5 | Online without API key | `agent.cpp:187` | **Partial** | [SRC] | Fatal exit, no recovery (P1-10) |
| 1.6 | Runtime mode switch | n/a | **Unsupported** | [SRC] | None. `mode_` is assigned once |
| 1.7 | CLI flags (`--help`, `--version`) | `main.cpp:11` | **Unsupported** | [SRC] | No `argv` parsing at all |
| 1.8 | `.env` loading | `config.cpp:23` | **Supported** | [SRC] | Silent; never reported to the user |

## 2. Chat and AI providers

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 2.1 | Ordinary chat | `agent.cpp:749` | **Supported** | [EXEC] | Reaches the provider; transport failure surfaces |
| 2.2 | Provider failure messaging | `ai_service.cpp:319` | **Partial** | [EXEC] | One template for 401, 400, 429, 5xx, and transport. The status code is duplicated |
| 2.3 | Retry, backoff, rate limits | n/a | **Unsupported** | [SRC] | Absent codebase-wide. `X-RateLimit` is parsed, then never read |
| 2.4 | Cost, token, quota reporting | n/a | **Unsupported** | [SRC] | `usage` is never read. Deliberately removed from `/stats` and probe-guarded |
| 2.5 | Offline (local Ollama) mode | `ai_service.cpp:41` | **Supported** | [EXEC] | Selectable; connection failure handled |
| 2.6 | Token accounting | n/a | **Unsupported** | [SRC] | Intentionally absent (R-10) |

## 3. File operations

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 3.1 | `read:` whole file | `agent.cpp:348` | **Supported** | [EXEC] | Read-only; path policy applied |
| 3.2 | `read:` line range | `agent.cpp:351` | **Supported** | [SRC] | Non-numeric input gives an explicit, useful error |
| 3.3 | `write:` new file | `agent.cpp:404` | **Supported** | [EXEC] | Creates parent directories silently |
| 3.4 | `write:` over existing file | `file_service.cpp:87` | **Partial** | [EXEC] | **Destroys content with 0 confirmations** (P0-4) |
| 3.5 | `replace:` text | `agent.cpp:423` | **Partial** | [SRC] | Guards on expected count, but is still unconfirmed, non-atomic, and has no backup |
| 3.6 | Path confinement | `validation.cpp:94` | **Partial** | [SRC] | Relative paths are confined to CWD. **Absolute paths are allowed outside 7 prefixes** |
| 3.7 | Symlink and traversal defence | `validation.cpp:114` | **Supported** | [SRC] | Canonicalization defeats both; unit-tested |
| 3.8 | Paths with spaces | `agent.cpp:406` | **Partial** | [SRC] | Impossible for `write:` and `/files`. The `@"..."` quoting is the only escape |
| 3.9 | `tree:` on a bad path | `codebase_service.cpp:143` | **Partial** | [EXEC] | **Prints nothing at all** (P2-3) |
| 3.10 | `todos:` on a bad path | `file_service.cpp:242` | **Partial** | [SRC] | Reports "no results" for an error |
| 3.11 | `components:` on a bad path | `codebase_service.cpp:99` | **Supported** | [SRC] | The only command in this group that reports the error |
| 3.12 | Binary file handling | `file_service.cpp:14` | **Partial** | [SRC] | No detection on `read:`; two divergent `is_text_file` definitions |
| 3.13 | Large file handling | `file_service.cpp:12` | **Partial** | [SRC] | 5 MB cap on `write:` and `read:`, but `read:` range has no cap |

## 4. Search and codebase analysis

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 4.1 | `grep:` via ripgrep | `search_service.cpp:158` | **Supported** | [EXEC] | Works; discloses its own truncation cap |
| 4.2 | `grep:` native fallback | `search_service.cpp:230` | **Partial** | [SRC] | 28-extension allowlist; silently skips many file types |
| 4.3 | `grep:` bad directory | `agent.cpp:493` | **Partial** | [EXEC] | Raw unthemed `rg` stderr leaks, then "No matches found" (P2-3) |
| 4.4 | Invalid regex | `file_service.cpp:208` | **Partial** | [SRC] | Silently reported as "no matches". `validate_regex_pattern` is never called (P2-4) |
| 4.5 | `search:` | `agent.cpp:315` | **Supported** | [SRC] | |
| 4.6 | `analyze:` | `agent.cpp:523` | **Supported** | [SRC] | Default `"."`. Tree depth 2 versus 3 for `tree:` is undocumented |
| 4.7 | Capability detection | `agent.cpp:93` | **Partial** | [SRC] | Detected once, never refreshed for the session |
| 4.8 | `Services::find_files` (`fd` backend) | `search_service.cpp:317` | **Unsupported** | [SRC] | Zero production callers; the truncation flag is computed, then discarded |

## 5. Shell and sandbox

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 5.1 | `!cmd` routing | `agent.cpp:283` | **Supported** | [EXEC] | Works when the line has no `@` |
| 5.2 | `cmd:` routing | `agent.cpp:329` | **Supported** | [EXEC] | Same router |
| 5.3 | Bare `ls` (no prefix) | `agent.cpp:308` | **Unsupported** | [SRC] | Sent to chat as a message; undocumented |
| 5.4 | `@` hijacking other commands | `agent.cpp:277` | **Partial** | [EXEC] | **`!echo hello@world` never ran** (P0-2) |
| 5.5 | Docker detection | `sandbox_service.cpp:130` | **Partial** | [EXEC] | `docker --version` only. A stopped daemon is reported as available (P1-2) |
| 5.6 | "Sandboxed" announcement | `agent.cpp:802` | **Partial** | [EXEC] | Claimed before verification; false under a stopped daemon |
| 5.7 | No-Docker fallback consent | `agent.cpp:809` | **Supported** | [EXEC] | Explicit, deny-by-default, names the consequence |
| 5.8 | Host execution output | `command_service.cpp:67` | **Partial** | [EXEC] | **stdout discarded. Prints `Command completed` with no output** (P1-1) |
| 5.9 | Host exit code reporting | `command_service.cpp:51` | **Partial** | [SRC] | Raw `pclose` status, for example `Exit code: 256` (P2-13) |
| 5.10 | Dangerous-command warning | `validation.cpp:221` | **Partial** | [SRC] | Denylist only. `git push --force` and `rm` variants bypass it |
| 5.11 | Sandboxed exit code and timeout | `sandbox_service.cpp:464` | **Supported** | [EXEC] | Correct, and honest when the container may survive |
| 5.12 | `/sandbox run` | `agent.cpp:2205` | **Partial** | [SRC] | Bypasses the router; asks no confirmation |
| 5.13 | `/sandbox status` | `agent.cpp:2240` | **Partial** | [EXEC] | Reports `Available` when the daemon is down |
| 5.14 | `/sandbox cleanup` | `sandbox_service.cpp:589` | **Partial** | [SRC] | Prunes by a label that is never set, so it cleans nothing |
| 5.15 | Cancellation and Ctrl-C | n/a | **Unsupported** | [SRC] | No signal handling. The process dies and state is lost (P2-12) |
| 5.16 | Session trust and opt-out flags | n/a | **Unsupported** | [SRC] | None exist, and there are no CLI flags at all |

## 6. Git and GitHub

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 6.1 | `git:log` | `agent.cpp:572` | **Supported** | [EXEC] | The 7-day window is hardcoded |
| 6.2 | `git:status` | `agent.cpp:574` | **Partial** | [EXEC] | Raw `--porcelain` with no legend. A missing `git` binary reads as "clean" |
| 6.3 | `git:analyze` | `agent.cpp:576` | **Partial** | [SRC] | Detached HEAD renders an empty branch line |
| 6.4 | `git:` path argument | `agent.cpp:570` | **Unsupported** | [EXEC] | **Silently ignored** (P1-9) |
| 6.5 | `git:diff`, `commit`, `push`, `branch` | n/a | **Unsupported** | [SRC] | Not implemented anywhere |
| 6.6 | Git shell injection defence | `git_service.cpp:13` | **Supported** | [SRC] | The denylist is sound for the double-quoted context; unit-tested |
| 6.7 | `github:repo/issues/health/threads` | `agent.cpp:594` | **Partial** | [SRC] | All read-only GETs |
| 6.8 | GitHub error reporting | `web_service.cpp:439` | **Partial** | [SRC] | HTTP status is ignored. A 401, 404, or 429 all read as JSON parse errors (P1-8) |
| 6.9 | GitHub auth | `github_service.cpp:13` | **Partial** | [SRC] | `GITHUB_TOKEN` only. Undocumented in `.env.example` |
| 6.10 | GitHub destructive operations | n/a | **Unsupported** | [SRC] | 10 write methods exist with no caller, so there is nothing to confirm |
| 6.11 | Review-thread parsing | `github_service.cpp:470` | **Supported** | [SRC] | Rigorous. Null-safe, paginated, capped, and honestly labeled |

## 7. Authentication and credentials

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 7.1 | Environment variable credential | `auth_service.cpp:455` | **Supported** | [EXEC] | Works for online mode |
| 7.2 | `.env` credential | `config.cpp:23` | **Supported** | [SRC] | Loaded into the process environment |
| 7.3 | `/auth key <provider> <key>` | `agent.cpp:2118` | **Partial** | [EXEC] | Accepted, but **inert** and **leaks the key in cleartext** (P0-3, P1-4) |
| 7.4 | `/auth set <provider>` | `agent.cpp:2103` | **Partial** | [SRC] | Accepted and confirmed, but inert (P1-4) |
| 7.5 | `/auth status` | `agent.cpp:2135` | **Partial** | [EXEC] | Reports a model never sent and a provider unrelated to the session (P1-5) |
| 7.6 | `/auth providers` | `agent.cpp:2089` | **Partial** | [EXEC] | The registry disagrees with the startup menu (P2-5) |
| 7.7 | `/auth test` | `agent.cpp:2151` | **Supported** | [EXEC] | Honest: "not verified, no live check performed" |
| 7.8 | Key validation | `auth_service.cpp:568` | **Partial** | [SRC] | Presence only. A malformed key is found by a failed request |
| 7.9 | Encrypted key store | `auth_service.cpp:616` | **Partial** | [SRC] | Encrypted, but double-encrypted on save, and files are written without permission hardening |
| 7.10 | Key never echoed by the app | `agent.cpp:2128` | **Supported** | [EXEC] | Confirmed. See 7.3 for the memory leak |

## 8. Memory

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 8.1 | `remember:<fact>` (global) | `agent.cpp:509` | **Supported** | [EXEC] | Works |
| 8.2 | `/memory add <text>` (global) | `agent.cpp:880` | **Supported** | [SRC] | Also global. The usage string is unreachable |
| 8.3 | `memory` (show global) | `agent.cpp:516` | **Supported** | [EXEC] | Global only. Session memory is not shown |
| 8.4 | `/memory show` (session and global) | `agent.cpp:878` | **Supported** | [EXEC] | Works |
| 8.5 | `clear` (session wipe) | `agent.cpp:520` | **Partial** | [EXEC] | No confirmation, and **triggered by ordinary prose** (P0-1) |
| 8.6 | `forget` (global file delete) | `agent.cpp:513` | **Partial** | [EXEC] | Deletes the file. No confirmation, and prose-reachable (P0-1) |
| 8.7 | `/memory` (bare) | `agent.cpp:860` | **Partial** | [SRC] | `Unknown meta command`, while siblings work (P2-9) |
| 8.8 | Automatic recording | `agent.cpp:418` | **Partial** | [EXEC] | Every command is recorded, including full file contents. Undisclosed |
| 8.9 | Memory bounds | `memory_manager.h:23` | **Unsupported** | [SRC] | Caps are declared, never enforced. `evict_old_entries` is empty |
| 8.10 | Memory read window | `memory_manager.cpp:698` | **Partial** | [SRC] | 100-line window. Older history is silently invisible |

## 9. Checkpoints

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 9.1 | `/checkpoint create` | `agent.cpp:1538` | **Partial** | [SRC] | Snapshots the whole CWD including `.env`, with no disclosure |
| 9.2 | `/checkpoint list` | `agent.cpp:1559` | **Supported** | [SRC] | Works. The reported size excludes metadata and the memory copy |
| 9.3 | `/checkpoint info <id>` | `agent.cpp:1578` | **Supported** | [SRC] | The best recovery hint in the product, since it points at the ID |
| 9.4 | `/checkpoint delete <id>` | `agent.cpp:1610` | **Partial** | [SRC] | Irreversible, with no confirmation and no trash |
| 9.5 | `/checkpoint cleanup [n]` | `agent.cpp:1628` | **Partial** | [SRC] | Reports success even when nothing was deleted |
| 9.6 | `/restore [id]` | `agent.cpp:894` | **Partial** | [SRC] | Works via `:896`. The `:894` branch is dead code |
| 9.7 | `/checkpoint restore <id>` | `agent.cpp:1638` | **Partial** | [SRC] | Undocumented, and not a true rollback (P2-8) |
| 9.8 | Restore safety net | `checkpoint_service.cpp:293` | **Partial** | [SRC] | A backup is created, but its path is never disclosed |
| 9.9 | Partial-restore reporting | `checkpoint_service.cpp:324` | **Partial** | [SRC] | Per-file failures are reported as success |
| 9.10 | Checkpoint unit tests | n/a | **Unsupported** | [SRC] | Zero test coverage for the entire service |

## 10. Sessions and compression

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 10.1 | `/chat save <tag>` | `agent.cpp:1116` | **Partial** | [SRC] | Silently overwrites an existing tag |
| 10.2 | `/chat resume <tag>` | `agent.cpp:1134` | **Partial** | [SRC] | Destroys unsaved conversation. No confirmation |
| 10.3 | `/chat list` | `agent.cpp:1145` | **Supported** | [SRC] | |
| 10.4 | Delete a saved session | n/a | **Unsupported** | [SRC] | No verb exists |
| 10.5 | `<tag>` path safety | `memory_manager.cpp:429` | **Partial** | [SRC] | Unvalidated. `../../x` escapes the directory |
| 10.6 | `/compress` availability | `agent.cpp:1221` | **Supported** | [EXEC] | Honest when there is too little history |
| 10.7 | `/compress` disclosure | `memory_manager.cpp:523` | **Partial** | [SRC] | Sends the transcript to the provider and truncates history. The loss is never quantified (P2-6) |
| 10.8 | `/context show/refresh/create` | `agent.cpp:1319` | **Partial** | [SRC] | `create` safely refuses to overwrite. The vocabulary is undocumented |

## 11. MCP

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 11.1 | `/mcp servers`, `/mcp status` | `agent.cpp:1738` | **Partial** | [SRC] | The config is real; the runtime state is fictional |
| 11.2 | `/mcp start`, `stop` | `agent.cpp:1773` | **Partial** | [SRC] | Honestly discloses the simulation |
| 11.3 | `/mcp tools`, `read`, `call`, `prompts` | `mcp_service.cpp:39` | **Unsupported** | [SRC] | Always empty or an error, yet still prints `Started MCP server:` (P1-7) |
| 11.4 | Runtime tool invocation | n/a | **Unsupported** | [SRC] | No transport. `send_mcp_request` returns hardcoded values |
| 11.5 | Server configuration | `mcp_service.cpp:20` | **Partial** | [SRC] | Hand-edit only. No validation, and it writes defaults on first use of `/mcp` |
| 11.6 | Documented capability | `README.md:7` | **Unsupported** | [SRC] | The README claims real MCP capability (P1-7) |

## 12. Help, statistics, errors, and exit

| # | Workflow | Entry point | Status | Evidence | Notes |
| --- | --- | --- | --- | --- | --- |
| 12.1 | `help` | `ui.cpp:80` | **Partial** | [EXEC] | Omits `/clear`, `/quit`, and `/exit`. Column alignment is broken |
| 12.2 | `/help`, `/?` | `agent.cpp:1047` | **Partial** | [SRC] | A different, non-overlapping list |
| 12.3 | `/tools` | `agent.cpp:1160` | **Partial** | [SRC] | A third list. Omits 2 of the 4 `/files` flags |
| 12.4 | Group helps (6 of them) | `agent.cpp:1508` and following | **Supported** | [SRC] | Every verb verified against its handler |
| 12.5 | Unknown meta command | `agent.cpp:912` | **Supported** | [EXEC] | Names the command and points at `/help` |
| 12.6 | Unknown direct command | `agent.cpp:741` | **Partial** | [EXEC] | A bare `Unknown command` with no echo, no suggestion, and no fallback (P1-3) |
| 12.7 | `/stats` | `agent.cpp:1280` | **Partial** | [EXEC] | Two fields. No memory, model, or credential state |
| 12.8 | `/error report`, `recent` | `agent.cpp:2300` | **Unsupported** | [EXEC] | Permanently `Total: 0 errors`, since `log_error` has no callers (P1-6) |
| 12.9 | `/error clear`, `export` | `agent.cpp:2322` | **Partial** | [SRC] | Export reports success even when the write failed |
| 12.10 | `/theme set` | `agent.cpp:2013` | **Partial** | [EXEC] | Succeeds and persists, but changes no rendering (P3-1) |
| 12.11 | `/sandbox status` | `agent.cpp:2240` | **Partial** | [EXEC] | Reports `Available` with the daemon down |
| 12.12 | Exit via `exit`, `quit` | `agent.cpp:127` | **Supported** | [EXEC] | `Goodbye` then `Agent run completed` |
| 12.13 | Exit via `/exit`, `/quit` | `agent.cpp:908` | **Partial** | [EXEC] | Skips `Agent run completed`. Two different exit texts |
| 12.14 | Case sensitivity | n/a | **Partial** | [SRC] | Fully case-sensitive, with no normalization |

## 13. State and safety summary

| # | Reported state | Accurate? | Evidence | Note |
| --- | --- | --- | --- | --- |
| 13.1 | Banner `Mode=` | Yes | [EXEC] | Recomputed from `mode_` |
| 13.2 | Banner `Model=` | **No** | [SRC] | Shows the provider name, not a model |
| 13.3 | Banner `Memory=Active` | **No** | [SRC] | Hardcoded, unprobed |
| 13.4 | Banner `Commands=Available` | **No** | [SRC] | Hardcoded, unprobed |
| 13.5 | `/stats Mode:` | Partly | [EXEC] | Provider name in the model field |
| 13.6 | `/auth status Provider:` | **No** | [EXEC] | Reads the store, not the session |
| 13.7 | `/auth status Model:` | **No** | [EXEC] | Never the model sent |
| 13.8 | `/sandbox status` | **No** | [EXEC] | Binary-only probe |
| 13.9 | `/theme current` | Yes | [EXEC] | Accurate, though the theme is unused elsewhere |
| 13.10 | `/checkpoint cleanup` result | **No** | [SRC] | Success is reported unconditionally |
| 13.11 | `remember:` confirmation | **No** | [SRC] | Reported even when the write failed |
| 13.12 | Host fallback consent | Yes | [EXEC] | Explicit and deny-by-default |

## 14. Destructive and irreversible operations

| Operation | Confirmation | Evidence | Verdict |
| --- | --- | --- | --- |
| `clear` (session memory) | **None** | [EXEC] | Destructive and prose-reachable |
| `forget` (global memory file) | **None** | [EXEC] | Destructive and prose-reachable |
| `write:` over an existing file | **None** | [EXEC] | Destructive, no backup |
| `replace:` in a file | **None** | [SRC] | Destructive, non-atomic |
| `/chat save` over an existing tag | **None** | [SRC] | Silent loss |
| `/chat resume` | **None** | [SRC] | Destroys unsaved conversation |
| `/compress` | **None** | [SRC] | Truncates history. Loss undisclosed |
| `/checkpoint delete` | **None** | [SRC] | Irreversible, no trash |
| `/checkpoint restore` | **None** | [SRC] | Overwrites. Backup path undisclosed |
| `/error export` over a file | **None** | [SRC] | Reports success on write failure |
| Host shell without Docker | **Yes** | [EXEC] | Correct. Names the consequence and denies by default |
| Sandboxed shell | **Yes (sandbox)** | [EXEC] | Withholds consent only when the daemon is down |
| Dangerous-command warning | **Yes (denylist)** | [SRC] | Bypassed by `git push --force` |
| Git and GitHub writes | n/a | [SRC] | Unreachable, because no write path exists |

## 15. Test coverage as a UX risk

| Area | Unit tests | Probes | Net gap |
| --- | --- | --- | --- |
| Command parsing (`Agent`) | **0**, since `agent.h` is not included | string-matching only | Let P0-1, P0-2, and P1-3 persist |
| `AuthService` | 0 | 5, covering presence and honesty only | Let P0-3, P1-4, and P1-5 persist |
| `AIService` | 0 | 0 direct | All endpoints, models, and error strings untested |
| `WebService` | 0 | 5, covering 200 and refused only | Let P1-8 persist |
| `ErrorService` | 0 | 1, on an empty log | The inert surface went undetected |
| `CheckpointService` | 0 | about 8 usage strings | No state-machine coverage |
| `ThemeService` | 0 | 1, for `list` | Persistence untested |
| Sandbox | 18 | about 8 | Strong, with one false-positive probe (P1-1) |
| GitHub parsers | 17 | 4 | Strong on parsers, none on HTTP status |
| Memory | 6 | 12 | Global memory and compression untested |

**Highest-value missing tests:** first, a command-parsing suite covering routing
precedence and verb-boundary matching; second, one asserting the user-visible
text for HTTP 401 and 403; third, one asserting that `/auth key` never writes
the key to memory.

---

*Pass 1 complete. Pass 2 not started, per protocol. Existing audit records
under `docs/audit/` were preserved unaltered.*