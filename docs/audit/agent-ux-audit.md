# Agentware: UX Audit (Pass 1)

Behavior-first audit of the Agentware terminal agent. UX is treated as the
behavioral skeleton; presentation is deliberately out of scope here and is
audited separately in Pass 2.

- **Commit audited:** `4caff2f7` (branch `main`)
- **Working tree at audit start:** clean (`git status --porcelain` empty)
- **Binary under test:** freshly configured and built from this tree
  (`cmake -S . -B /tmp/audit-build -DCMAKE_BUILD_TYPE=Release`, exit 0).
  No stale binary was used as evidence.
- **Runtime harness:** hermetic sandbox (`HOME`/`USERPROFILE` redirected to a
  temp dir, `env -i`, offline mode selected). No real API key was used; no live
  provider call was made except one deliberate localhost connection failure.
- **Scope of change:** none. This pass modified no source, test, config, or
  documentation. The tree was verified clean before and after.

Every finding below is tagged with how it was established:

- **[EXEC]**: reproduced by running the freshly built binary; transcript or
  quoted output included.
- **[SRC]**: established by source inspection at the cited `file:line`; not
  separately reproduced at runtime.
- **[UNVERIFIED]**: could not be established in this environment; stated
  explicitly rather than asserted.

---

## 0. Remediation pass status

Pass 1 was accepted on review. A test-first remediation pass followed. The
finding text below is preserved exactly as audited; this section records what
changed afterwards.

### Addressed in the remediation pass

| Finding | Change |
| --- | --- |
| P0-1 prose wipes memory | Verb matching moved to a complete-verb, valid-argument-boundary rule in `src/command_router.cpp`. `clear`, `forget`, and `memory` accept no arguments, so `clear: the screen please` and `clearance-notes: x` are no longer commands |
| P0-2 `@` hijacks commands | `classify_input` evaluates `!`, `/`, bare verbs, and colon commands before any file reference. `is_file_reference_at` requires `@` to open a token, so email addresses are no longer read as file names |
| P0-3 keys written in cleartext | `redact_secrets` strips the value from `/auth key` before it reaches memory, logs, or checkpoint copies |
| P0-4 `write:` overwrites silently | Replacing an existing file now requires explicit `y`/`Y`. Creating a new file is unchanged |
| P1-1 host fallback hides output | Host execution captures stdout. The false-positive probe now asserts `HELLO_FROM_SHELL` instead of the substring `hi`, which matched inside "**thi**s" in the consent banner |
| P1-2 sandbox claimed pre-verification | `is_docker_available` now queries the daemon with `docker info` instead of the CLI version, so a stopped daemon routes to the explicit host fallback. `/sandbox status` distinguishes a missing CLI from an unreachable daemon |

### Recorded as follow-up (not started)

These remain open by decision, to keep the batch narrow. Ordered by
operational urgency.

| # | Follow-up | Detail |
| --- | --- | --- |
| 1 | Bound the Docker availability probe | `SandboxService::spawn_capture` has no timeout. A hung `docker info` blocks command dispatch indefinitely: verified still running after 14s with no output. It does **not** create an unsafe host fallback, but it makes the agent unresponsive. The probe should return control and state that availability could not be established, keeping the existing explicit consent policy |
| 2 | Define a redaction policy for shell-command history | Redaction covers the `/auth key` family only. The `!` and `cmd:` paths persist raw commands verbatim, so `!export TOKEN=...` or `!curl -H "Authorization: Bearer ..."` reaches memory and every checkpoint in cleartext. Verified: `cmd:echo TOKEN-VIA-CMD-PATH` was stored verbatim. This needs a deliberate policy, not fragile pattern matching. Agentware must not be described as protecting secrets in persisted command history |
| 3 | Fall back to chat on unmatched colon commands | Also resolves the one routing regression this batch introduced: mixed prose containing both `@` and a colon (`explain @notes.txt: what is this`) now yields `Unknown command` where it previously injected and chatted. This follows from evaluating `:` before file references. A chat fallback would route it to chat, where injection then applies |
| 4 | Correct or implement `/auth set` | Accepted and confirmed, yet inert: `AIService` is built once and never rebuilt. Either rebuild it or label the command as applying to future sessions |
| 5 | Make `/auth status` report actual state | It shows a model that is never sent and a provider read from the auth store rather than the live session |
| 6 | Wire `/error` diagnostics | `ErrorService::log_error` has no callers, so `/error report` and `/error recent` are permanently empty |
| 7 | Handle HTTP status codes and rate limits | `fetch_with_headers` computes `success` from the curl result alone, so 401, 404, and rate-limit 403 all read as JSON parse errors. Rate-limit headers are parsed and never read; there is no retry or backoff |
| 8 | Correct the Windows CI job | See the limitations section. This is a CI defect, not a product defect |

### Test coverage added

The original audit called out that `main_test.cpp` deliberately avoids
including `agent.h`, leaving command parsing with no unit coverage. The
dispatch rules now live in `src/command_router.cpp` behind a header that does
not pull in `Agent`, so they are unit tested directly. Six test suites cover
routing, verb boundaries, at-sign boundaries, and redaction, and 24
end-to-end probes cover the same rules through the real binary.

---

### 1.1 What Agentware is

Startup prints a logo (`src/ui.cpp:54-77`) and a system line
(`src/ui.cpp:283-287`):

```
System: Mode=Offline | Model=llama3.2:latest | Memory=Active | Commands=Available
```

It is a REPL that mixes four distinct interaction families in one input line,
plus free-form chat:

| Family | Trigger | Handler |
| --- | --- | --- |
| File injection | `@` **anywhere** in the line | `src/agent.cpp:277` |
| Shell | leading `!` | `src/agent.cpp:283` |
| Meta command | leading `/` | `src/agent.cpp:289` |
| Direct command | **any** `:` anywhere in the line | `src/agent.cpp:305` |
| Bare verbs | exact `forget`/`memory`/`clear` | `src/agent.cpp:298-302` |
| Chat | anything else | `src/agent.cpp:308` |

The families are discoverable only through three overlapping help screens
(`UI::print_help` at `src/ui.cpp:80`, `show_meta_help` at `src/agent.cpp:1047`,
`show_available_tools` at `src/agent.cpp:1160`) plus a duplicated quick-help
strip at startup. `src/main.cpp:11` takes no `argv`, so there is no `--help` or
`--version`; those words are only reachable once the REPL is running.

### 1.2 The routing rules are the dominant UX problem

The dispatcher is a first-match-wins prefix chain, and two of its predicates
test for a character **anywhere** in the line rather than at the start. This
produces the P0 findings in section 4.

### 1.3 Consistency of naming and structure

Naming is broadly consistent in *notation*. Meta commands use `/`, direct
commands use `verb:`, and shell uses `!`. The six group help screens
(`/checkpoint`, `/mcp`, `/theme`, `/auth`, `/sandbox`, `/error`) were each
verified verb by verb against their handlers and match.

Consistency breaks at the seams:

- **Argument conventions are three different parsers.** Direct commands split
  on **colon** (`read:`, `replace:`, `grep:`) but `write:` splits on the **first
  space** (`src/agent.cpp:406-409`), with no escaping anywhere. Meta commands use
  `std::istringstream`. There is no quoting for direct commands, so a path or
  replacement containing a space or colon cannot be expressed.
- **Numeric-argument validation is inconsistent.** `read:` and `replace:` emit
  explicit errors for non-numeric input (`src/agent.cpp:372-373`, `:450-452`);
  `/checkpoint cleanup [n]` and `/error recent [n]` silently fall back to a
  default (`src/agent.cpp:1629-1632`, `:2304-2307`).
- **All commands are case-sensitive**, with no normalization anywhere in the
  dispatch path. `Read:foo` and `/HELP` fall through to chat or to
  `Unknown command`.

### 1.4 Overlaps, dead modes, and unimplemented advertised capability

- **`clear` is overloaded across two families.** `/clear` clears the screen
  (`src/agent.cpp:872`); bare `clear` truncates session memory
  (`src/agent.cpp:520`). Only the bare form is documented in `help`
  (`src/ui.cpp:107`); only the slash form appears in `/help`.
- **`/memory` (bare) is broken** while every sibling bare verb works, because
  it is missing from `spaced_verbs` (`src/agent.cpp:860-862`), so `/memory`
  returns `Unknown meta command: /memory`. **[SRC]**
- **Dead code confirmed by inspection.** `agent.cpp:894` is unreachable because
  the `/restore` verb is space-normalized before dispatch. `ui.cpp:170-202`
  (`print_enterprise_status`) and `ui.cpp:299-306` (`prompt_user`) are never
  called. `agent.cpp:1024-1045` (`should_skip_file`) is never called. Five
  unused `Validator` functions are dead, including `validate_regex_pattern`,
  whose absence is why an invalid regex is reported as "no matches".
- **MCP is a stub with no runtime capability.** `send_mcp_request`
  (`src/mcp_service.cpp:39-77`) returns hardcoded empty arrays and a
  `-32601 Method not found`; `start_mcp_server` sets `is_running = true` and
  `process_id = getpid()` without executing anything (`src/mcp_service.cpp:100-103`).
  `README.md:7` states the agent "reads and edits code through MCP filesystem
  and git servers" and `README.md:34` says the shipped servers "run through
  `npx`". Neither is true. The `/mcp start|stop|status` layer does disclose
  the simulation honestly (`src/agent.cpp:1789-1792`); the
  `/mcp tools|read|call|prompts` layer does not, and still prints
  `Started MCP server: <name>` (`src/mcp_service.cpp:105`). **[SRC]**

---

## 2. Command inventory verified against the parser

Direct (colon) commands: `search:`, `cmd:`, `read:`, `write:`, `replace:`,
`grep:`, `remember:`, `forget`, `memory`, `clear`, `analyze:`, `components:`,
`todos:`, `git:`, `tree:`, `github:` (`src/agent.cpp:315-742`).

Meta (slash) commands: `/help`, `/?`, `/clear`, `/chat`, `/tools`, `/memory`,
`/compress`, `/stats`, `/context`, `/files`, `/fetch`, `/checkpoint`,
`/restore`, `/mcp`, `/theme`, `/auth`, `/sandbox`, `/error`, `/quit`,
`/exit` (`src/agent.cpp:853-915`).

`git:` sub-verbs are exactly `log`, `status`, and `analyze`
(`src/agent.cpp:572-577`). **`git:diff`, `git:commit`, `git:push`,
`git:branch`, and `git:checkout` do not exist.** `github:` sub-verbs are
exactly `repo:`, `issues:`, `health:`, and `threads:`, all read-only GETs.

Help and parser mismatches confirmed:

| Mismatch | Evidence |
| --- | --- |
| `/clear`, `/quit`, `/exit` absent from `help` | implemented `src/agent.cpp:872,908`; `help` is `src/ui.cpp:80-167`. **[EXEC]**: 0 matches for `/clear` or `/quit` in `help` output |
| `help` and `/help` print different, non-overlapping lists | `src/ui.cpp:80` vs `src/agent.cpp:1047` |
| `/files` documents 2 of 4 implemented flags | `--no-gitignore` (`src/agent.cpp:1372`) and `--no-recursive` (`:1375`) appear in no help text |
| `/checkpoint restore` implemented but undocumented | `src/agent.cpp:1638`; help lists only `/restore` |
| `/error export <path>` shows `<path>` as required; it is optional | help `src/agent.cpp:2288`; code defaults at `:2330` |
| `/auth set` documented as functional but inert | `src/agent.cpp:2068` |

---

## 3. Core workflow traces

### 3.1 Startup, mode selection, first prompt

Works, with one fatal gap.

`Config::load_environment` reads `.env` silently (`src/config.cpp:23-65`), logo
prints, then `initialize_mode()` (`src/agent.cpp:144-231`) asks:

```
Mode [1=Online / 2=Offline] (default 2):
```

and, for online, a provider prompt defaulting to Together AI; for offline, a
model prompt defaulting to `llama3.2:3b`.

Empty input and EOF both take the default; invalid input loops with
`[WARN] Invalid choice, please try again.` **[EXEC]**

**Fatal gap:** choosing Online without the environment variable set throws and
the program exits with `Fatal error: TOGETHER_API_KEY not set`, after the logo
and prompts have already been printed, with no way back to Offline
(`src/agent.cpp:187-189`, `src/main.cpp:29-32`). **[SRC]**

Mode selection is one-shot: `mode_` is assigned only at `src/agent.cpp:185`
and `:225`. There is **no runtime mode switch**, so 9 of 10 `AgentMode` values
are reachable and `MODE_UNSET` is not.

### 3.2 Ordinary chat and provider failures

One undifferentiated error.

Chat funnels into a single `AIService::chat`. Every failure, whether a bad key
(401), a bad request (400), a rate limit (429), a provider outage (5xx), or a
transport fault, collapses into one template (`src/ai_service.cpp:319-327`).
`response.error_message` then duplicates the status code, producing text of the
form:

```
Error: AI service returned status code 401 - <body> | Error: HTTP 401 | Error: <curl message>
```

**[EXEC]** (transport path, offline mode with no local Ollama):
```
> Error: AI service returned status code 0 -  | Error: HTTP 0 | Error: Failed to
  connect to localhost port 11434 after 0 ms: Couldn't connect to server
```

There is **no retry, no backoff, and no rate-limit handling anywhere** in the
codebase. No cost, token, or quota information is surfaced. Every one of these
error strings is written to `data/memory.txt` (`src/agent.cpp:783`) and sent
back to the provider as context on the next turn.

### 3.3 File read, write, replace, search

Read is safe; write is unguarded.

`read:` is read-only and validated. `write:` and `replace:` mutate the
filesystem with **no confirmation, no backup, no dry-run, and no
refuse-overwrite**.

**[EXEC]** `write:` overwrote an existing file containing
`ORIGINAL IMPORTANT CONTENT` with zero prompts; output was only
`File 'precious.txt' written successfully (25 bytes)`. A follow-up run
confirmed **0** confirmation prompts.

Path policy is real but permissive: `Validator::validate_file_access`
(`src/validation.cpp:94-150`) canonicalizes and confines *relative* paths to
the CWD, but **absolute paths anywhere outside seven hard-coded prefixes are
allowed** (`/etc`, `/sys`, `/proc`, `/dev`, `/private/etc`, `c:/windows`,
`c:/system32`). This is a CWD-relative check, not a jail. **[SRC]**

Failure reporting is uneven:

| Input | Observed or coded result |
| --- | --- |
| `grep:pattern:/no/such/dir` | **[EXEC]** raw unthemed `rg: /no/such/dir: IO error ...` leaks to the terminal, **then** `No matches found for pattern: pattern`. A hard error is reported as an empty result |
| `tree:/no/such/dir` | **[EXEC]** prints **nothing at all** |
| `todos:/no/such/dir` | **[SRC]** `No task comments found`, so an error is reported as an empty result |
| `components:/no/such/dir` | **[SRC]** correctly reports `Error: cannot list components in ...`. This is the only one that does |
| invalid regex | **[SRC]** silently reported as "no matches"; `validate_regex_pattern` is never wired in |
| `/files` truncation | **[SRC]** hits 100 files / 10 MB / 1 MB with **no marker**, unlike `grep:` which discloses its cap |

### 3.4 Shell, sandboxing, and the no-Docker fallback

Consent is explicit; output is lost.

The router is `execute_routed_command` (`src/agent.cpp:799-819`).

**Docker available:** announces `Sandboxed (docker, default config, no network)`
and runs. Detection is `docker --version` **only**, with no daemon ping and no
image check (`src/sandbox_service.cpp:130-134`).

**[EXEC]** With the Docker CLI installed but the daemon stopped, the tool
claimed isolation and then failed:
```
> Sandboxed (docker, default config, no network)
failed to connect to the docker API at unix:///var/run/docker.sock ...
Exit code: 1
```
The isolation claim is made *before* the run and is never verified or
corrected. `/sandbox status` reports `Docker Status: Available` in exactly this
state. There is no host fallback on this branch, so the failure is at least not
silent, but the pre-claim is false. **[EXEC]**

**Docker absent:** the fallback is **explicit and deny-by-default**, which is
the one part of the sandbox design that is fully right:
```
> Sandbox unavailable (Docker not found): this command would run on the host without isolation.
Run on host without isolation? (y/N):
```
**[EXEC]** On consent (`y`), the output was `Command completed`, meaning
**the command's stdout was never displayed.** `CommandService::execute`
redirects stdout to `/dev/null` (`src/command_service.cpp:67-68`), so a host
`!ls` or `!cat file` appears to succeed while producing nothing. The single
probe intended to catch this (`remediation_probes.sh:420-421`) greps for `hi`,
which matches inside "**thi**s command would run on the host" in the consent
banner itself, so it is a false positive that leaves the host path unverified.

Container policy is otherwise reasonable: `--cap-drop ALL`,
`--security-opt no-new-privileges`, `--pids-limit 256`, and `--network=none`.
The CWD is mounted **read-write** at `/workspace`
(`src/sandbox_service.cpp:45-52`), and containers run as root. `/sandbox run`
bypasses the router and asks **no** confirmation at all.

**No signal handling exists anywhere.** Ctrl-C kills the process, losing
session state and any running container. There is no cancel command and no
interruptible wait.

### 3.5 Git and GitHub

Read-only, with two real defects.

Local git is three read-only commands via `popen`. **`git:` ignores its path
argument** because `path` is hardcoded `"."` (`src/agent.cpp:570`).

**[EXEC]** `git:status /definitely/not/a/repo` returned the status of the
current directory, with no warning.

GitHub is four read-only commands, all requiring `GITHUB_TOKEN` from the
environment. `.env.example` never documents `GITHUB_TOKEN`.

**The HTTP layer reports success on failure.** `fetch_with_headers` sets
`success = (res == CURLE_OK)` and ignores the HTTP status entirely
(`src/web_service.cpp:439`). Every 4xx and 5xx therefore returns
`success == true`, so all `if (!response.success)` guards in
`github_service.cpp` are dead.

**[SRC]** Consequence: a missing or invalid token, a 404 repo, and a
rate-limit 403 all surface as a JSON parse complaint such as
`Error: Error parsing issues: expected a JSON array of issues`. The user is
never told the token is missing, the repo is absent, or a limit was hit.
`X-RateLimit-Remaining` is parsed into the header map and then never read.

There is **no destructive or irreversible operation in the entire git or
GitHub surface**, because every reachable method is a read. Ten write methods
(`create_issue`, `update_issue`, `add_comment`, `create_pr_comment`, and
others) exist but have no caller, so there is no confirmation anywhere on this
path because there is nothing to confirm. **[UNVERIFIED]** the reachable
surface is confirmed by inspection only; the PATCH-becomes-GET bug in
`make_github_request` (`src/github_service.cpp:43-46`) is latent and
unreachable.

### 3.6 Authentication

Keys are stored, echoed into memory, and inert.

Three credential paths exist: environment, `.env` (loaded into the process
environment at `src/main.cpp:14`), and runtime `/auth key`.

**Credential exposure [EXEC]:**
```
$ /auth key together SUPER-SECRET-KEY-123
$ grep SUPER-SECRET-KEY-123 data/memory.txt
User: /auth key together SUPER-SECRET-KEY-123
```
`src/agent.cpp:2175` records the raw command line for every `/auth`
invocation, so any key typed at runtime is persisted in **cleartext** to
`data/memory.txt`, and is then copied into **every subsequent checkpoint**
(`src/checkpoint_service.cpp:162-167`). The app itself never echoes the key,
and the encrypted store in `data/auth_config.json` is never written on this
path.

**The command is inert.** `/auth key` and `/auth set` are accepted, confirmed,
and have **zero effect** on the running session: `AIService` is constructed once
from `Agent::api_key_` on first chat (`src/agent.cpp:750-752`) and never
rebuilt. `AuthService::get_api_key()` has no callers.

**Reported state is wrong [EXEC]:**
```
> Provider: Together AI
Status: Key present (not verified)
Model: meta-llama/Llama-3.2-3B-Instruct-Turbo
```
The model shown is **never the model sent**, since `src/ai_service.cpp:162`
sends `Llama-3.3-70B-Instruct-Turbo-Free`. The provider shown is read from the
auth store rather than the live session, so an agent started in offline mode
with `llama3.2` still reports "Together AI".

**Provider registries disagree.** Startup offers Together, Cerebras, Fireworks,
Groq, DeepSeek, and OpenAI; the auth registry holds together, ollama, openai,
anthropic, and cerebras (`src/auth_service.cpp:401-443`).
`/auth set fireworks` fails with `Error: Provider not found: fireworks`, while
`ollama` and `anthropic` are settable but unusable.

Keys are validated by **presence only** (`src/auth_service.cpp:568-586`), so a
malformed key is discovered only by a failed HTTP request. Files are written
with no permission hardening.

### 3.7 Memory, global memory, and clearing

This is the P0 data-loss surface.

Two stores, both undocumented in the UI:

| | Session | Global |
| --- | --- | --- |
| Path | `data/memory.txt` | `$HOME/.agentware/AGENTWARE.md` |
| Filled by | **every command automatically** | `remember:`, `/memory add` |
| Cleared by | `clear` | `forget` |

`remember:` and `/memory add` write the **global** file; `memory` shows **global
only**; session memory is visible only via `/memory show`. The caps
(`max_memory_size`, `max_entries`) are declared and **never enforced**;
`evict_old_entries` is an empty stub (`src/memory_manager.cpp:813-816`), so
`data/memory.txt` grows without bound and the user is never told older history
has become invisible.

**Every command is auto-recorded, including full file contents and command
output** (`src/agent.cpp:418`, `:459`, `:508`), with no disclosure.

### 3.8 Checkpoints

Restore is not a rollback.

`/checkpoint create` snapshots the **entire CWD tree** into
`data/checkpoints/<id>/`, including `.env` and any secrets, because the skip
list covers only version control, build, and cache directories
(`src/checkpoint_service.cpp:54-64`).

`/checkpoint restore` overwrites files and `data/memory.txt` with no
confirmation. It does create a `pre-restore-<id>` backup
(`src/checkpoint_service.cpp:293-298`), which is the only safety net and is
itself a full tree copy, but it **never tells the user where that backup is**.
Files deleted since the checkpoint are **not** removed, so a restore is not a
true state rollback, and nothing says so. Per-file failures are `std::cerr`
warnings the agent never surfaces, after which the command **reports success**
(`src/checkpoint_service.cpp:324-342`).

`/checkpoint delete` is irreversible with no confirmation and no trash.

### 3.9 Sessions and context compression

Silent, lossy, and outbound.

`/chat save|resume|list` stores a byte copy of `data/memory.txt` under
`data/conversations/`. `resume` copies it back over the live file,
**destroying everything said since the save**, with no confirmation. There is
no verb to delete a saved session. The `<tag>` is unvalidated, so
`/chat save ../../x` writes outside the directory. **[SRC]**

`/compress` requires at least 5 interactions, then **sends the raw recent
transcript to the configured provider** with no notice, backs up
`data/memory.txt`, and truncates it. Everything older than the last 100 lines
is dropped from the live file. The user is told only:
```
Context successfully compressed!
Original interactions have been summarized and recent context preserved.
Backup created automatically for safety.
```
The backup path is never given, and the loss is never quantified. **[SRC]**

### 3.10 Help, statistics, errors, and exit

- `/stats` reports two fields: `Mode` and `Commands Processed`. No memory size,
  no model actually in use, no credential state. **[EXEC]**
- `/error report` and `/error recent` are **permanently empty**, because
  `ErrorService::log_error` has **zero call sites**, so nothing is ever
  recorded. **[EXEC]** after `/auth status`: `Error Summary (Total: 0 errors)`.
  The whole diagnostics surface is therefore non-functional, and
  `data/error_log.json` is never written.
- The startup banner hardcodes `Memory=Active` and `Commands=Available`
  (`src/ui.cpp:286-287`) with no probe behind either. **[SRC]**
- Exit is inconsistent: bare `exit` prints `Goodbye` then
  `Agent run completed`; `/exit` prints `Goodbye!` and calls `exit(0)`,
  skipping the second line. **[EXEC]**

---

## 4. Findings by severity

### P0: unsafe behavior and data loss

**P0-1. Ordinary English sentences with a colon silently wipe memory.**
`process_user_input` routes any line containing `:` to the direct-command
handler (`src/agent.cpp:305`), which matches `clear`, `forget`, and `memory` by
**prefix** with `rfind(...,0)` (`src/agent.cpp:513,516,520`). Neither operation
prompts.

**[EXEC]**
```
> read:demo.txt          (records an interaction)
> clear: the screen please
> Session memory cleared
> /memory show
  Current memory context:
  Global memories:
  - the sky is blue
  Recent interactions:            <- empty
```
**[EXEC]** The global store is equally exposed:
```
> remember:secret fact alpha
> forget: nothing important
> Global memory cleared
> No global memories stored
```
The source acknowledges the prefix matching as a known quirk
(`src/agent.cpp:294-297`), but the exact-match guard at `:298-302` protects only
the colon-less path.

*Remedy:* require the verb to be the complete first colon field before treating
it as a command, and add a confirmation to both clears.

*Regression test:* feed `clear: <sentence>` and `forget: <sentence>` and assert
memory survives.

**P0-2. `@` anywhere hijacks shell, meta, and direct commands.**
`src/agent.cpp:277` tests for `@` **anywhere**, before the `!`, `/`, and `:`
checks. **[EXEC]** `!echo hello@world` did not run the shell at all; it was
routed to AI chat and returned a provider connection error. A `!` command
containing any `@` (an email, an `@file` reference) is silently transformed into
a chat message.

*Remedy:* match `@` only at the start of the token, and check `!` and `/`
first.

**P0-3. Runtime API keys are written to disk in cleartext.**
`src/agent.cpp:2175` records the whole `/auth` command line into session memory.
**[EXEC]** `/auth key together SUPER-SECRET-KEY-123` produced the literal line
`User: /auth key together SUPER-SECRET-KEY-123` in `data/memory.txt`, which is
then copied into every checkpoint.

*Remedy:* redact the argument to `/auth key` before recording; exclude memory
files from checkpoint capture.

**P0-4. `write:` destroys existing files with no confirmation.**
**[EXEC]** A file containing `ORIGINAL IMPORTANT CONTENT` was replaced by
`write:` with zero prompts and only `File 'precious.txt' written successfully`.
`ContextService::create_context_file` refuses to overwrite
(`src/context_service.cpp:286-289`), so the safe pattern already exists in the
codebase.

*Remedy:* require confirmation when the target exists, or add a
refuse-overwrite default.

### P1: core workflow broken or materially misleading

**P1-1. Host shell fallback discards all command output.** **[EXEC]** After
consenting to run without isolation, `!echo HELLO_FROM_SHELL` produced only
`Command completed`. `src/command_service.cpp:67-68` redirects stdout to
`/dev/null`. The probe meant to catch this is a false positive.

*Remedy:* capture stdout; fix the probe to assert on a distinctive token.

**P1-2. Sandbox isolation is claimed before it is verified.** **[EXEC]** With
the daemon down the tool printed `Sandboxed (docker, default config, no network)`
then failed to connect. `/sandbox status` likewise reports `Available`.

*Remedy:* probe the daemon (`docker info`) rather than the binary.

**P1-3. Any colon in prose yields `Unknown command`.** **[EXEC]**
`What time is it: now` produced `Unknown command`, with no fallback to chat, no
echo of the input, and no suggestion.

*Remedy:* on no prefix match, fall back to chat.

**P1-4. `/auth key` and `/auth set` have no effect.** Accepted and confirmed,
yet the live `AIService` is never rebuilt (`src/agent.cpp:750-752`).

*Remedy:* rebuild the service, or label both commands as persisting for future
sessions.

**P1-5. `/auth status` misreports provider and model.** **[EXEC]** Reports a
model that is never sent and a provider unrelated to the running session.

**P1-6. The `/error` surface is inert.** **[EXEC]** `Total: 0 errors` even
after a session containing failures, because `log_error` has no callers. The
user has no working way to review what went wrong.

*Remedy:* wire `log_error` at the failure sites that already produce
user-visible errors.

**P1-7. MCP has no runtime capability but is documented as if it does.**
`README.md:7` and `:34` describe reading and editing through MCP servers that
never run.

*Remedy:* correct the README; the `/mcp` disclosure layer is already honest for
start, stop, and status.

**P1-8. GitHub errors are misreported as JSON parse failures.**
`src/web_service.cpp:439` ignores HTTP status. **[SRC]** A 401, 404, or
rate-limit 403 all surface as `expected a JSON array of issues`. Rate limits are
invisible, since there is no retry, no backoff, and no `X-RateLimit` handling
anywhere.

**P1-9. `git:` ignores its path argument.** **[EXEC]**
`git:status /definitely/not/a/repo` reported the current directory.

**P1-10. Startup cannot recover from a missing API key.** **[SRC]** Choosing
Online without the variable exits the program after the prompts have printed,
with no route back to Offline.

### P2: significant friction, inconsistency, and recovery difficulty

**P2-1.** Help is fragmented across three non-overlapping screens. `/clear`,
`/quit`, and `/exit` are absent from `help` **[EXEC]**. `/files` documents 2 of
4 flags, and `/checkpoint restore` is undocumented.

**P2-2.** `/files` and `@` truncate at their caps with no marker, unlike
`grep:` which discloses truncation. **[SRC]**

**P2-3.** Hard errors are reported as empty results. `tree:` prints nothing at
all **[EXEC]**, `todos:` says "no task comments found" **[SRC]**, and `grep:`
leaks raw `rg` stderr unthemed before claiming "no matches" **[EXEC]**.

**P2-4.** Invalid regex is indistinguishable from no matches, because
`validate_regex_pattern` exists but is never called. **[SRC]**

**P2-5.** Provider registries disagree between the startup menu and `/auth`.
`/auth set fireworks`, `groq`, and `deepseek` all fail. **[SRC]**

**P2-6.** `/compress` sends the transcript to the provider without notice and
truncates history without disclosing the loss or the backup location. **[SRC]**

**P2-7.** `/chat resume` silently destroys unsaved conversation, and
`/chat save` silently overwrites an existing tag. **[SRC]**

**P2-8.** Checkpoint restore reports success despite per-file failures, never
discloses the backup path, and is not a true rollback. **[SRC]**

**P2-9.** Bare `/memory` returns `Unknown meta command` while `/tools`,
`/stats`, `/compress`, and `/clear` all work bare. **[SRC]**

**P2-10.** Inconsistent numeric validation: explicit errors for `read:` and
`replace:`, silent defaults for `/checkpoint cleanup` and `/error recent`.
**[SRC]**

**P2-11.** `command_count_` counts all input including blank lines and chat, so
`/stats` over-reports, while `help`, `version`, `exit`, and `quit` bypass it.
**[SRC]**

**P2-12.** No Ctrl-C handling: the process dies, losing session state and any
running container. No cancel command exists. **[SRC]**

**P2-13.** Host fallback reports a raw `pclose` status, so a failing command
shows `Exit code: 256`. **[SRC]**

**P2-14.** Banner hardcodes `Memory=Active` and `Commands=Available`. **[SRC]**

### P3: minor friction and polish

**P3-1.** `/theme set dark` succeeds and `/theme current` reports it, but it
changes nothing about the interface, because the UI uses hardcoded ANSI
constants and never consults `ThemeService`. **[EXEC]**

**P3-2.** `read:` documentation differs between `help` and `/tools`
(`[:start[:count]]` versus `[:start:count]`), and the `github:threads` grammar
is written three different ways.

**P3-3.** Error messages carry a leading space, producing a double space after
the `> ` prompt.

**P3-4.** Help column alignment is broken for the longest entries.

**P3-5.** Known dead code with no user impact today: `ui.cpp:170-202`,
`ui.cpp:299-306`, `agent.cpp:1024-1045`, `agent.cpp:894`, five unused
validators, `database_service.cpp` (compiled, never instantiated), and the
`/mcp` "already running" branch that can never be reached because
`load_server_config` resets state on every invocation.

---

## 5. What is genuinely well done

Credit where the behavior holds up under scrutiny:

- **No-Docker fallback is explicit and deny-by-default.** The user is told
  exactly what will happen and must actively consent. No silent escalation.
- **Sandbox hardening is real**: `--cap-drop ALL`,
  `--security-opt no-new-privileges`, `--pids-limit 256`, `--network=none`,
  no docker socket, and only the CWD mounted.
- **Local git injection is properly defended**: `is_safe_shell_path` blocks
  quote-breakout metacharacters, and a unit test asserts a breakout attempt
  does not execute.
- **GitHub access is read-only and shell-free**, so no injection path exists
  and no irreversible operation is reachable.
- **Review-thread parsing is rigorous.** Null-field degradation, outdated
  anchors, pagination caps, and truncation are all handled and unit-tested, and
  the output honestly states that anchors are comment-time positions.
- **`/stats` was deliberately stripped of token reporting** and a probe
  regression-guards that removal.
- **`NO_COLOR`, piped-mode spinner suppression, and pty color behavior** are
  implemented and covered by probes.
- **Docker-dependent tests are hermetic** by construction: stub binaries,
  temp `PATH`, temp cwd, and restoration afterwards.

---

## 6. Known limitations of this audit

- **Coverage gap in the test suite itself.** `src/tests/main_test.cpp`
  deliberately does not include `agent.h`, so **no unit test exercises any
  command parsing**. `CheckpointService` has zero unit tests, and
  `AuthService`, `AIService`, `WebService`, `ErrorService`, and `ThemeService`
  each have zero unit tests. Most behavioral coverage comes from
  string-matching shell probes.
- **No live provider call was made**, so exact provider error bodies and the
  `usage` payloads are **[UNVERIFIED]**.
- **Windows and containerized runs were not exercised.** This pass ran on macOS
  only, so platform-conditional behavior is **[UNVERIFIED]**.
- **Cross-platform test coverage is not established, and the CI job cannot
  establish it.** The Windows build job runs
  `cmake --build build --target preflight --config Release || true`. The
  trailing `|| true` masks build failures, and CTest never runs on Windows at
  all, so neither the unit tests nor the remediation probes execute there.
  Windows CI gates compilation only. The new routing code uses portable
  standard-library headers and the edited test stubs sit inside pre-existing
  `#ifdef _WIN32` guards, but source inspection is not a Windows test run and
  must not be reported as one.
- **The probe suite was verified with ripgrep shadowed out** (136/136 pass), so
  a CI image without ripgrep exercises the native search fallback. This was run
  on macOS only.
- **[UNVERIFIED]** The belief that `std::async`'s future destructor blocks the
  host timeout path (`src/command_service.cpp:72-76`) was not reproduced.
- **The 100-line memory window and 5-second cache staleness** are code-path
  findings; the visible effect was not timed.
- **Two probes in `remediation_probes.sh` contradict their own header comment.**
  Lines 5 and 6 claim "No network is required" while lines 154 to 161 make live
  calls to `api.github.com`.

---

## 7. Recommended next batch

Ordered by dependency. **No changes have been made.**

**Batch 1: data loss and credential exposure (independent, all small):**

1. Require complete-verb matching before `clear`, `forget`, and `memory`
   dispatch (P0-1), and add confirmation to both clears.
2. Match `@` only at token start; evaluate `!` and `/` first (P0-2).
3. Redact `/auth key` arguments before recording to memory; exclude memory
   files from checkpoint capture (P0-3).
4. Require confirmation when `write:` targets an existing file (P0-4).

**Batch 2: shell correctness (depends on nothing):**

5. Capture host-fallback stdout; fix the false-positive probe (P1-1).
6. Probe the Docker daemon, not the binary, before claiming isolation (P1-2).

**Batch 3: routing and reporting:**

7. Fall back to chat on an unmatched colon command (P1-3).
8. Honor HTTP status in `fetch_with_headers` and surface rate limits (P1-8).
9. Wire `ErrorService::log_error` at existing failure sites (P1-6).

**Batch 4: consistency, after items 1 to 3 stabilize the routing model:**

10. Consolidate help into one screen; fix `/clear`, `/quit`, and the `/files`
    flags.
11. Report the model actually sent; reconcile `/auth status` with the session.
12. Make `/theme` affect rendering or remove it.

**Cross-cutting test work:** add unit coverage for command parsing (currently
zero), for `AuthService` persistence, and for HTTP status handling. These three
gaps let P0-3, P1-4, and P1-8 persist undetected.

---

*Pass 2 (terminal presentation audit) has not been started, per the audit
protocol. The existing UI records under `docs/audit/` were preserved
unaltered.*