# Remediation plan

Post-audit remediation for the highest-risk confirmed defects. Baseline is
the four artifacts in `docs/audit/`; this file adds the minimal fix plan
the audit called for. No application code was modified to produce it, and
each defect below was confirmed by reading the implementation plus a live
run of the built binary where noted.

Scope: session crashes, silent result loss, unenforced path checks,
null-service handling, and commands that report success without doing the
advertised work. Out of scope (tracked as decisions, not defects):
whether MCP gains a real transport, whether PostgreSQL stays, which
search backend is canonical, the release flow, `Core::Agent` structure,
and new dependencies. The audit artifacts are unchanged; nothing found in
this pass contradicts them.

## R-01 `read:` with a non-numeric range crashes the session, High

Evidence: `src/agent.cpp:317-322` calls `std::stoi` on user input with no
guard, in a branch that only checks file existence (`src/agent.cpp:309`).
Nothing between the handler and `main` catches, so `main` prints and
exits 1 (`src/main.cpp:29-32`).

Confirmed live: `read:README.md:abc` in offline mode prints
`Fatal error: stoi: no conversion`, exit 1.

Affected path: `run()` loop → `process_user_input()` →
`handle_direct_command()` `read:` branch →
`FileService::read_file_range()` (never reached).

User impact: any mistyped range kills the session; on-disk memory
survives, but in-memory context and the operator's flow do not.

Minimal fix: validate the range tokens before conversion, reusing the
existing `Validator` result shape. Non-numeric or non-positive input sets
an `Error:`/`Usage:` result string and skips the service call. No behavior
change for valid input.

Regression tests: piped-REPL probes asserting exit 0 plus an `Error:`
line for `read:README.md:abc`, `read:README.md:1:xyz`, and
`read:README.md:-1`; valid-range cases must read unchanged.

## R-02 `replace:` with a non-numeric count crashes the session, High

Evidence: `src/agent.cpp:372` calls `std::stoi(parts[3])` with no guard.
Same unwind path as R-01.

Affected path: `handle_direct_command()` `replace:` branch →
`FileService::replace_text_in_file()` (never reached).

User impact: same session kill as R-01, on a mutating command where the
operator most needs a clean error.

Minimal fix: same validation-first pattern as R-01. Default stays `1`;
non-numeric input yields an error string.

Regression tests: piped-REPL probe for `replace:README.md:old:new:abc`
asserting exit 0 and an `Error:` line; valid-count cases unchanged.

## R-03 `/compress` segfaults when no chat has run, High

Evidence: `compress_context()` dereferences `ai_service_`
unconditionally (`src/agent.cpp:977`). The pointer is assigned only in
`handle_ai_chat()` (`src/agent.cpp:536-538`). All four `ai_service_`
derefs in the file were mapped; the two inside `handle_ai_chat`
(`src/agent.cpp:540,558`) are safe, this one and R-05 are not.

Confirmed live: five `read:` commands (seeding five interactions without
creating the AI service) followed by `/compress` prints
`Generating AI-powered summary...` then dies with exit 139 (SIGSEGV).
The short-history early return (`src/agent.cpp:956-961`) masks the bug in
fresh sessions, which is why smoke tests never caught it.

Affected path: `handle_meta_command()` → `compress_context()` →
`AIService::chat()` on a null `unique_ptr`.

User impact: hard crash with no error message; strictly worse than
R-01/R-02.

Minimal fix: null-check `ai_service_` (and `is_available()`) at the top
of `compress_context()` and print a plain unavailable message instead.
Same guard shape already used in `handle_ai_chat`
(`src/agent.cpp:540-543`).

Regression tests: piped-REPL probe seeding five `read:` interactions then
`/compress`, asserting exit 0 and the unavailable message; a chat-seeded
`/compress` probe asserting unchanged behavior (summary path, mocked or
offline-graceful).

## R-04 `/fetch` text format silently discards the result, High

Evidence: the default text branch assigns the fetched body and returns
before printing or saving (`src/agent.cpp:1151-1153`).

Confirmed live: `/fetch http://127.0.0.1:9 text` prints `Fetching: ...`
and nothing else, exit 0. The operator cannot distinguish success,
failure, or truncation.

Affected path: `handle_meta_command()` → `handle_web_fetch_command()`
text branch (ends at the early `return`).

User impact: the most common fetch mode is a silent no-op; fetched
content never reaches the operator or memory.

Minimal fix: remove the early return so the text branch flows into the
existing print/save/AI-summary tail shared with the other formats. No new
output format; the `raw` branch is the template.

Regression tests: piped-REPL probe against an unreachable URL asserting
the `Error:` line appears; a probe against a local mock asserting fetched
text is printed and saved (memory file grows).

## R-05 `/fetch` large-content AI path has the same null deref, High

Evidence: `src/agent.cpp:1163` calls `ai_service_->chat()` with no guard,
reachable via `/fetch <url> json` (or `raw`) returning over 1000 chars
before any chat input.

Affected path: `handle_web_fetch_command()` → AI-summary branch.

User impact: same SIGSEGV class as R-03 on a read-only command.

Minimal fix: same null/`is_available()` guard as R-03; when unavailable,
print the fetched content (truncated as today) without AI summarization.

Regression tests: piped-REPL probe fetching >1000 chars with no prior
chat, asserting exit 0 and printed content; a chat-seeded probe asserting
the summary path still runs.

## R-06 File commands bypass the safe-path check, High

Evidence: `Validator::is_safe_path` rejects `..`, `/etc/`, `/sys/`,
`/proc/`, `/dev/`, and `C:\Windows\` (`src/validation.cpp:243`), and
`Validator::validate_file_path` exists (`src/validation.cpp:11`), but a
repo-wide grep finds zero call sites for either in `agent.cpp`. The
`read:`/`write:`/`replace:` handlers call only `validate_file_exists`
and `validate_file_writable`. The guard the codebase wrote for exactly
this threat is dead code on the paths that need it.

Affected paths: `handle_direct_command()` `read:`/`write:`/`replace:`
branches; also `@` injection via `read_file_or_directory`
(`src/agent.cpp:712`).

User impact: an agent-driven session can read or overwrite sensitive
files the validator was designed to block.

Minimal fix: call `is_safe_path` (or `validate_file_path`) on the
resolved filename at the top of each file-mutating and file-reading
branch, before existence checks, returning the validator's error on
failure. Pure gate addition; no path-resolution redesign.

Regression tests: GTest cases on `is_safe_path` for traversal, absolute
sensitive paths, and normal relative paths; piped-REPL probes asserting
`read:/etc/hostname` and `write:/tmp` outside-scope equivalents are
refused with exit 0 (exact refusal paths to be fixed at implementation
time against the validator's rule list).

## R-07 MCP lifecycle reports state it never creates, Medium

Evidence: `/mcp start` prints `started (simulated)` without calling any
service (`src/agent.cpp:1442-1453`); `/mcp stop` prints `stopped
(simulated)` the same way (`src/agent.cpp:1455-1466`). One layer down,
`start_mcp_server` sets `is_running = true` and records the agent's own
PID as the server's (`getpid()`, `src/mcp_service.cpp:97-106`), and
`send_mcp_request` returns empty arrays for every list method
(`src/mcp_service.cpp:63-68`). So `status` and `call`/`tools`/`resources`
present fabricated state as fact.

Affected paths: `handle_mcp_command()` start/stop/status/resources/tools/
call branches; `MCPService` lifecycle and request functions.

User impact: operators believe tool servers run and return empty
capabilities; any workflow built on MCP output is founded on fiction.
The CLI `(simulated)` label discloses simulation at start/stop only ,
`status`, `tools`, and `call` carry no such label.

Minimal fix (no transport work): make every MCP output honest. `start`
and `stop` keep the simulated label and must not imply a live process;
`status` reports `simulated` rather than `running`; list/call commands
state that no transport exists instead of returning empty arrays shaped
like real results. Do not invent a transport here, that is the Phase 3
architectural decision.

Regression tests: piped-REPL probes asserting `/mcp start`/`status`/
`tools list` output contains the simulated/no-transport wording and never
a bare `running` claim; GTest cases on `send_mcp_request` error shape for
unknown vs known-simulated servers if the honesty wording is centralized.

## R-08 `/auth` reports trust it never verifies, Medium

Evidence: `validate_credentials` and `test_provider_connection` return
key non-emptiness, never touching the network
(`src/auth_service.cpp:568-586,714-728`); `is_credential_secure`
unconditionally returns `true` (`src/auth_service.cpp:755-758`); key
generation is an empty placeholder (`:760-763`). `get_provider_status`
therefore prints `Connected` for any non-empty string
(`src/auth_service.cpp:731-746`).

Affected paths: `/auth` status/test handlers → `AuthService` validation
and status functions.

User impact: operators trust dead keys; a typo'd key reads `Connected`
until a real chat fails opaquely.

Minimal fix (no network probing): relabel honestly. `Connected` becomes
`Key present (not verified)` or equivalent; `is_credential_secure`
reports the actual posture (encryption configured vs plaintext `.env`)
instead of constant `true`. Real connectivity probing is deferred to the
architectural phase, where its timeout and privacy implications belong.

Regression tests: GTest cases asserting status wording for empty vs
non-empty keys; piped-REPL `/auth` probe asserting the word `Connected`
no longer appears without verification.

## R-09 `search:` demands a key its backend never uses, Medium

Evidence: `WebService::search` requires `SERPAPI_KEY`, then queries the
DuckDuckGo Instant Answer endpoint with the key appended as a query
parameter (`src/web_service.cpp:43-67`). SerpAPI is never contacted, so
the key buys nothing and is disclosed to a third party for no benefit.

Affected path: `handle_direct_command()` `search:` branch →
`WebService::search()` → DuckDuckGo HTTPS GET.

User impact: paid-key holders get DuckDuckGo quality while believing
they pay for SerpAPI; the key leaks into third-party request URLs.

Minimal fix (no backend swap): stop sending `&api_key=` to DuckDuckGo
and correct the missing-key message to describe the actual backend. Which
backend is canonical stays a Phase 3 decision; this item only stops the
misleading exfiltration and messaging.

Regression tests: code-level assertion or logged-URL probe showing no
`api_key` parameter leaves the process; probe of `search:` without the
env var asserting the corrected message.

## R-10 Dead telemetry and no-op recovery handlers, Low

Evidence: `token_usage_` is printed in `/stats` but never incremented
(repo-wide grep: declaration `src/include/agent.h:30`, sole read
`src/agent.cpp:1008`); `register_recovery_handler` is a no-op and
`attempt_auto_recovery` trivial (`src/error_service.cpp:229-248`), with
an explicit `return false; // Not implemented` at `:239`.

Affected paths: `/stats` output; `/error` recovery commands.

User impact: operators read `Token Usage: 0` and recovery confirmations
that measure nothing.

Minimal fix: remove the `Token Usage` line (or wire the counter, removal
is smaller and honest); make recovery commands print `not available`
instead of implying action. One-line-class edits, no framework.

Regression tests: `/stats` probe asserting no `Token Usage` line (or a
nonzero counter after chats, if wired); `/error` recovery probe
asserting the unavailable wording.

## Explicitly excluded from this remediation

- `get_user_choice` `std::stoi` (`src/agent.cpp:224-232`) is already
  guarded by try/catch with re-prompt; verified safe, no work needed.
- `handle_ai_chat` derefs (`src/agent.cpp:540,558`) are safe by
  construction (created at `:536-538`); verified, no work needed.
- `DatabaseService`, MCP transport, search-backend choice, release flow,
  `Core::Agent` structure, and `--version`/HEALTHCHECK (slated for the
  release-readiness phase) are architectural items, not defects for this
  pass.
- No new dependencies are introduced by any item above.

## Explicit non-goals (documented, not silently kept)

- `@` inside email addresses: any input containing `@` is routed to file
  injection, so `contact me at foo@bar.com` is rewritten to
  `foo[File not found: bar.com]` before reaching the model (verified live:
  the marker is substituted and the session continues normally). Parser
  semantics are intentionally unchanged by this series; fixing address
  handling would broaden the change surface and belongs to a dedicated
  parser task, not a security remediation.

## Dependencies and acceptance criteria

- R-01 and R-02 share one validation pattern; implement together, test
  together. No other item depends on them.
- R-03 and R-05 share one guard pattern (`ai_service_` null plus
  `is_available()`); implement together. R-04 is independent but lives in
  the same function, so reviewing R-04/R-05 as one diff is recommended.
- R-06 is independent of the crash fixes but should land before any
  E2E expansion that drives file commands against fixtures.
- R-07, R-08, R-09, R-10 are independent honesty fixes; each is a
  wording-plus-test change with no cross-dependencies.
- Global acceptance: every defect has a failing-before/passing-after
  regression test; the full `ctest` suite plus the E2E compose run pass;
  no public command changes observable behavior except where this plan
  says it does.

## Recommended implementation order

1. R-03 and R-05 (null-service guards), segfault first; smallest diff,
   two probes.
2. R-01 and R-02 (input validation), same pattern, same tests, crash
   class eliminated in one pass.
3. R-04 (`/fetch` text output), one early-return removal plus probes;
   review with R-05.
4. R-06 (path enforcement), gate addition with GTest plus REPL probes.
5. R-07, R-08, R-09, R-10 (honesty fixes), wording plus tests, any
   order, batchable.

## Review-gate additions (scope extensions with evidence)

These items were added during the final review gate. Each was confirmed
live; none changes the charter (trustworthy existing behavior first).

### R-06 extended: same gate on every file-reading sink

Tracing every user-supplied path found four more sinks with no check:
`@`-injection (`read_file_or_directory`, content leak into LLM context
confirmed live via memory read-back), `grep:` directory (content leak),
`todos:` (content leak), `analyze:`/`components:`/`tree:` (structure
metadata), and `/files` paths. `git:` hardcodes `"."` and is safe. All
seven sinks now call `validate_file_access` with the same refusal shape.
Fail-before: 9/9 new probes fail on original code (secret present in
memory); pass after.

### R-08b: GitHub availability decoupled from the search key

`GitHubService::is_available()` required `WebService::is_available()`,
i.e. a `SERPAPI_KEY`, for GitHub operations. The function has zero
callers, so no user-visible behavior changes; it now depends only on
`GITHUB_TOKEN`. Covered by an env-isolated GTest (POSIX; skipped on
Windows). `WebService::is_available()` itself is now unused but left in
place as public API.

### R-11: `find_project_root` hung outside projects (new, High)

`ContextService::find_project_root` looped while
`has_parent_path()`; at the filesystem root `parent_path()` returns root
itself on this toolchain, so the loop never ended. Any chat started from
a directory with no project markers above it spun forever in
`is_project_root` (confirmed via `sample` stacks: ~97% of samples in
`filesystem::__status`). This blocked the review-gate probes, which run
in marker-less temp dirs. Fix: break when the parent equals the current
path. Covered by `FindProjectRootTest` plus every chat-bearing probe
(which hang pre-fix). Session impact pre-fix: full REPL hang, worse than
the crashes in R-01/R-02.

## Uncertainties for review

1. Test seams: `Agent`'s handlers are private methods on a 2009-line
   class, and several commands read interactive `y/N` from stdin. The
   piped-REPL probes above work without structural change (verified
   pattern this session), but GTest-level cases for R-01/R-02/R-06 are
   only possible if parsing/validation is factored into small free
   functions or `Validator` additions. That factoring is a minimal,
   behavior-preserving extraction, not a redesign, confirm it is
   acceptable before implementation, otherwise those items stay
   probe-tested only.
2. R-06 rule strictness: the exact refusal set (validator rules vs
   additionally blocking absolute paths outside the project root) needs
   an owner decision; over-blocking breaks `@` injection of legitimate
   absolute paths, under-blocking keeps the hole.
3. R-07 wording vs removal: if the MCP decision leans toward removal,
   the honest-labeling work here is throwaway. Confirm whether to label
   now or skip R-07 to the architectural phase.
4. R-09 backend: the minimal fix stops the key leak but leaves the
   backend question open by design; confirm the DuckDuckGo-actual
   messaging is acceptable interim copy.
5. Interactive `y/N` confirmations read from `std::cin` mid-command,
   which piped probes satisfy via scripted lines but which EOF-defaults
   in automation; any probe touching `cmd:` confirmation paths must
   encode that explicitly.
