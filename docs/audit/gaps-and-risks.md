# Gaps and risks

Prioritized by practical impact. Every item cites repository evidence.
Severity: Critical (data loss, security boundary, or release-blocking),
High (crash or silently wrong behavior), Medium (drift, confusion, or
future breakage), Low (hygiene).

## G-01 File commands never enforce the safe-path check — High

`Validator::is_safe_path` rejects `..`, `/etc/`, `/sys/`, `/proc/`,
`/dev/`, and `C:\Windows\` (`src/validation.cpp:243`), but the
`read:`/`write:`/`replace:` handlers only call `validate_file_exists` and
`validate_file_writable` (VERIFIED `src/agent.cpp:300-334`). The existing
guard is dead code on the paths that need it, so an agent-driven session
can read or overwrite sensitive files the validator was written to block.

## G-02 Unguarded parsing crashes the session — High

- `read:file:abc` reaches `std::stoi` with no guard
  (`src/agent.cpp:317-322`); `std::invalid_argument` escapes to `main`,
  which prints `Fatal error` and exits 1 (`src/main.cpp:29-32`). The same
  pattern appears in the `replace:` handler (`src/agent.cpp:372`).
- `/compress` before any chat dereferences a null `ai_service_`
  (`src/agent.cpp:977` vs lazy creation in `handle_ai_chat`). A new user
  following the `/help` menu can crash on their first command.

## G-03 `/fetch` text output is silently discarded — High

The default text branch assigns the fetched result and returns before
printing or saving (`src/agent.cpp:1151-1153`). The most common fetch mode
does nothing observable. The large-content AI path below it
(`src/agent.cpp:1163`) shares the G-02 null-service hazard.

## G-04 Web search charges a key it never uses — Medium

`WebService::search` requires `SERPAPI_KEY`, then queries the DuckDuckGo
Instant Answer endpoint with the key appended as a query parameter
(`src/web_service.cpp:43-67`). SerpAPI is never contacted, so search
quality is whatever DuckDuckGo infers, and a paid key is exfiltrated into
third-party URLs for no benefit.

## G-05 MCP is simulated, not connected — Medium

`send_mcp_request` returns empty mock arrays, server start only flips a
flag, and the agent announces `started (simulated)` (`src/mcp_service.cpp:
39-113`, `src/agent.cpp:1448,1465`). The `/mcp` surface and
`data/mcp_servers.json` (filesystem/git via `npx`) imply real tool servers
that do not exist. Any workflow built on MCP tools silently does nothing.

## G-06 Shell construction invites injection — High

`GitService` concatenates paths into `cd "path" && git ...` with no
escaping (`src/git_service.cpp:24,52,80`); a path containing `"` breaks
out. `SandboxService` passes volume strings straight into `docker run -v`
(`src/sandbox_service.cpp:171-262`), so a malicious sandbox config mounts
arbitrary host paths. Command allow-listing is substring warnings plus a
narrow regex denylist (`src/validation.cpp:124`,
`src/command_service.cpp:14-28`); `curl|sh`, `chmod -R`, and `python -c`
style payloads are unblocked. Only a `grep system\(` preflight line watches
this area (`src/scripts/preflight.sh:325`).

## G-07 Auth reports security it does not verify — Medium

Encryption itself is real AES-256-GCM (`src/auth_service.cpp:267-400`),
but connection testing checks only key non-emptiness (`:714-729`),
`is_credential_secure` unconditionally returns `true` (`:756`), and key
generation is a placeholder (`:759-762`). `/auth status` output overstates
assurance, and keys also live in plaintext `.env`.

## G-08 Database support is present but unreachable — Medium

`DatabaseService` compiles to `return false/nullptr` without `HAVE_PQXX`
(`src/database_service.cpp:37-100`), ships stub `pqxx::` types to keep
building (`src/include/database_service.h:11-26`), carries a per-OS SQL
quoting fork (`src/database_service.cpp:71-134`), and has zero callers
(repo-wide grep). `.env.example` advertises five `DB_*` vars for a feature
nothing invokes. Release builds omit `libpqxx` entirely
(`release.yml:38-50`), so the same binary silently differs by job.

## G-09 Dead code and dead metrics mislead operators — Low

`token_usage_` is printed but never incremented (`src/include/agent.h:30`,
`src/agent.cpp:1008`); `should_skip_file` and `safe_get_string` are
defined but never called (`src/agent.cpp:771`,
`src/ai_service.cpp:247`); `ErrorService` recovery registration is a no-op
(`src/error_service.cpp:248`). `/stats` and `/error` output imply
instrumentation that does not exist.

## G-10 Tests assert almost nothing — Critical

Two GTest asserts cover ~12.9k LOC (`src/tests/main_test.cpp:1-16`);
`make test` and CMake `check` assert existence and clean exit only. The
only behavioral tests are six `expect` substring matches over mocks, gated
to `main`/dispatch, warn-only in preflight, with no negative, schema, or
offline-model cases. Codecov and Windows preflight cannot fail the build.
Any refactor ships without a safety net; this is the single largest risk
to every other fix proposed in the roadmap.

## G-11 Release pipeline is tripled and partly stale — Medium

`ci.yml` VERSION-diff tagging, `version-bump.yml` label-driven bumps,
`release.yml` tag builds, `security.yml` tarball publishing, plus local
`release.sh`/`prepare-release.sh` overlap. `prepare-release.sh:63-69`
greps for the old workflow name and a build path that no longer matches,
so it fails as written. `CHANGELOG.md` `0.1.5`/`0.1.6` are bare `*`, and
`unmerged origin/version-bump-0.1.7` vs `VERSION` `0.1.6` shows the flow
already stalled once. CPack names a `-dev` runtime dep.

## G-12 Health and version signals are vacuous — Medium

`main()` parses no arguments, so `agentware-agent --version` starts the
REPL; on closed stdin it exits 0 without printing a version (verified
during this audit), and on a TTY it blocks at the prompt. The Docker
`HEALTHCHECK` therefore always succeeds while checking nothing, and there
is no machine-readable version path at all.

## G-13 Platform and reproducibility pins are loose — Low

System-first dependency resolution plus unpinned apt/brew/choco/vcpkg
installs let environments diverge; the CMake cache key hashes only
`CMakeLists.txt`. `__DATE__`/`__TIME__` and tag-derived versions make
bit-identical builds impossible. Apple `arm64 -march=armv8.4-a` excludes
Intel Macs; the pre-commit clang-format absolute path breaks Linux; the
Docker builder still sets `git http.sslVerify false`.

## G-14 Documentation describes a smaller project — Low

`README.md` says small dev tools with no usage, examples, or command
reference for a 9-provider agent with 30+ commands. `docs/` holds only
`index.html`; the deleted `ARCHITECTURE.md`, `COMMANDS.md`, `SETUP.md`,
and packaging guides were never replaced. `SECURITY.md` supports a `1.x`
line that does not exist, leaving the shipped `0.1.6` nominally
unsupported.

## Unresolved decisions

1. Is MCP intended to gain a real stdio/JSON-RPC transport, or should the
   simulated surface be removed? (Evidence: G-05.)
2. Is PostgreSQL support a goal or vestigial? Nothing calls it, yet CI and
   Docker install it everywhere. (Evidence: G-08.)
3. Which search backend is canonical, DuckDuckGo or SerpAPI? The code uses
   one and bills the other. (Evidence: G-04.)
4. Should the release flow be label-driven bumps, VERSION-diff tagging, or
   manual tags? All three are live. (Evidence: G-11.)
5. Who is the documented user? Collaborators-only contributing plus a
   marketing page leaves operators and external contributors without
   guidance. (Evidence: G-14.)
