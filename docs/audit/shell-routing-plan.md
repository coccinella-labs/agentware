# Shell routing plan

Trace-first plan for routing agent-requested shell execution through the
existing Docker sandbox. Read-only analysis; no application code changed.
The `github:threads` commit is untouched. Scope: reuse the existing
backend, leave read-only paths alone, add no backends, keep `github:threads`
unchanged.

## 0. Approved decisions (recorded before implementation)

1. **Docker unavailable: deny by default.** No silent host fallback. At
   the existing confirmation point, explain the sandbox is unavailable
   and offer an explicit one-command opt-in to unsandboxed execution
   worded as running with host permissions; cancellation stays default.
2. **Fix the `.` denylist entry.** Replace the overbroad substring rule
   with structural tests; defense in depth only, never a substitute for
   isolation. Other rules stay unless tests prove them wrong.
3. **Remove the dead shell-mode display.** `shell_mode_` never affects
   dispatch; drop the `/stats` line and the toggle implication. No new
   shell-mode semantics.
4. **Required hardening, not optional:** quote every config field into
   the Docker invocation (plus config validation for volumes/image/
   workdir); run containers with dropped capabilities,
   no-new-privileges, and pid limits while keeping network/resource
   restrictions; enforce real timeouts that stop the container;
   `allowed_commands` stays unenforced and unclaimed; mutable image tags
   documented with digest pinning deferred to an owner decision; no
   Docker/host failure may fall back silently.
5. **Tests:** pure backend selector, extracted Docker-command builder,
   missing-Docker/malformed-config/timeout/failure cases, all without a
   live daemon.

## 1. Execution paths traced (all VERIFIED in code)

### Path A: `!cmd` — host execution

`process_user_input` (`src/agent.cpp:280`) → `handle_shell_command`
(`src/agent.cpp:774-805`): trim, usage on empty, `validate_command_safe`
warnings + `y/N` confirmation, then `CommandService::execute` (`:802`).

### Path B: `cmd:...` — host execution

`handle_direct_command` (`src/agent.cpp:316-334`): identical
validate/confirm shape, then `CommandService::execute` (`:333`).

### `CommandService::execute` (`src/command_service.cpp:59-81)

Regex denylist (9 tokens) → host `popen` with stderr merged → 5-second
`std::async` timeout → 10KB truncation. Runs with the agent's uid,
environment, network, and filesystem. No isolation of any kind.

### Dead end: shell mode

`!` alone toggles `shell_mode_` (`src/agent.cpp:964-967`), which is
displayed in `/stats` (`:1229`) but **never read in dispatch**:
`process_user_input` (`:270-297`) routes every line by prefix regardless
of the flag. "Shell Mode: Active" promises behavior that does not exist.
Recommend removing the display (and retargeting `!` alone to usage) as
part of this change; it is the same honesty class as R-07/R-10.

### Path C: `/sandbox run <cmd>` — the only sandboxed path

`handle_sandbox_command` (`src/agent.cpp:2107-2129`): no
`validate_command_safe`, no confirmation → `SandboxService::
execute_command(cmd)` with the `default` config → substring denylist
(`is_command_safe`) → `docker run`. Note the inconsistency: the
sandboxed path asks less than the host path. Confirmation must stay
(consent), isolation is added on top.

### Config provenance

No `/sandbox create` handler exists; configs come only from
`data/sandbox_config.json` on disk (inside the R-06 root, so the LLM's
own `write:` can modify them — anyone who can write the config can
already write the agent's memory, but a hostile config must never be
trusted blindly; see gaps).

## 2. Sandbox boundary audit (`execute_command`, `:171-262`)

Sound today: `--rm`, unique names, memory/CPU limits, `--network=none`
by default, empty volume list by default, `/workspace` workdir, env
values and the command shell-escaped, denylist gate before launch.

Gaps, each cited:

1. **Root in container, no hardening flags.** No `--user`, `--cap-drop`,
   `--security-opt=no-new-privileges`, `--read-only`, or `--pids-limit`.
   A container breakout lands further than it should.
2. **Unescaped config fields in a host-shell string.** `workdir`, env
   KEYS, image, and volumes are concatenated into `docker run ...`
   executed via host `popen`; only env values and the command use
   `escape_shell_arg`. A hostile `sandbox_config.json` is host shell
   injection. `validate_sandbox_config` (`:464`) checks only
   name/image/limits and has zero callers.
3. **Dead policy field.** `allowed_commands` is defaulted and persisted
   but never consulted by `execute_command`.
4. **Denylist both over- and under-blocks.** Substring matching with a
   bare `"."` entry blocks nearly every realistic command containing a
   dot (`cat README.md`, `python script.py`) — pushing users toward host
   exec — while missing whole classes (`chmod -R`, `chown`, `apt-get`,
   compiler toolchains).
5. **Timeout measured, not enforced.** `timeout_seconds` is compared
   after the fact into a flag; the `fgets` loop blocks until Docker
   exits. A hung container hangs the REPL (`CommandService` has a real
   5s async timeout; the sandbox has none).
6. **Mutable image tags pulled on demand** (`ubuntu:22.04`,
   `alpine:latest`): registry trust + network at run time, non-pinned
   contents.
7. **Latent injection in `docker cp` helpers**: `container_name`
   unescaped (`:327-350`). Safe today (internally generated), fragile
   tomorrow.
8. **Duplicated presence checks**: private `is_docker_available` and
   public `check_docker_installation` do the same `docker --version`
   string match.

## 3. Proposed behavior (three cases + routing)

New pure function `select_execution_backend(docker_available, policy)`:

- **Docker available.** `!cmd` and `cmd:` run via
  `SandboxService::execute_command` (default config) after the existing
  warnings + confirmation. Output maps `SandboxResult` to the current
  string shape (stdout, exit code, timeout notice). Report backend and
  limits honestly, e.g. `Sandboxed (docker, default, no network)`.
- **Docker unavailable.** Deny by default with a diagnostic pointing at
  `/sandbox status` — but reuse the existing `y/N` moment as the
  explicit policy gate: on refusal, offer host execution with wording
  that states no isolation applies
  (`Docker unavailable - run on host without isolation? (y/N)`).
  Never silent, never labeled safe.
- **Docker execution failure** (daemon error, missing image, timeout).
  Report the error distinctly; do not fall back to host execution.
  Enforce timeouts: run Docker under a future timeout and `docker stop`
  the container on expiry.

Hardening inside scope: `--user nobody`-style drop, `--cap-drop ALL`,
`--security-opt no-new-privileges`, `--pids-limit`, escape ALL config
fields (or exec without a host shell), validate volumes/image/workdir in
`validate_sandbox_config` and call it on load, enforce don't just
measure timeouts. `allowed_commands` stays unused unless a decision
revives it — do not half-wire it.

## 4. Test plan (no live Docker daemon)

- `select_execution_backend` matrix (available/unavailable ×
  policy × failure): pure GTest, all three cases.
- Docker-command builder extraction (same approved pattern as
  `try_parse_int`): GTest asserts flags, escaping of hostile
  workdir/image/volume/env values, and regression for each gap fix.
- `is_command_safe` pins current behavior; the `"."` entry needs an
  owner decision (bug vs policy) before changing.
- Config validation: GTest `validate_sandbox_config` including hostile
  volumes/images once checks exist.
- Probes with Docker forced absent (`PATH` without docker):
  `!cmd` refusal wording, explicit host opt-in wording, `/sandbox
  status` honesty, exit-0 session survival.
- Live-Docker runs remain manual/CI-gated; nothing in the suite
  requires a daemon.

## 5. Out of scope / residuals

New backends, read-only path changes, `github:threads`, the email-`@`
quirk, TOCTOU (unchanged class), image pinning policy (needs owner),
`allowed_commands` revival (needs decision), `copy_*` hardening beyond
noting it, Windows/macOS backend parity beyond honest reporting.

## 6. Implementation record (deviations and findings)

- Quoting, not `execvp`: `popen` is retained for Windows portability, but
  every config-derived field is single-quoted, verified empirically
  (`dash` executes a hostile value as one inert argument, no file
  created). Stated plainly wherever the mechanism matters.
- Privileges: `--cap-drop ALL`, `--security-opt=no-new-privileges`,
  `--pids-limit 256` added. `--user`/`--read-only` deliberately not set:
  the default workdir must stay writable, so they are infeasible without
  restructuring the workspace layout. Documented, not silently skipped.
- WEXITSTATUS decoding was missing (found by a failing probe: daemon
  errors reported `Exit code: 256`). Fixed in the sandbox path only;
  `CommandService` keeps its pre-existing raw-status behavior (out of
  scope, noted).
- Routing lives in a static `execute_routed_command` in `agent.cpp`
  (no new selector type): Docker present → sandbox with backend report;
  absent → denial + explicit host opt-in reusing the `y/N` moment;
  failure → distinct error, no silent fallback. Covered by
  `PATH`-controlled and fake-`docker` probes, including daemon-failure
  output. Live timeout enforcement has no daemon-free probe (default
  300s floor); rendering plus review cover it — residual, stated.
- Default configs mount the agent CWD at `/workspace:rw`; the loader
  skips built-ins, so the mount cannot be stripped via config file.
  Custom volumes/images/workdirs are structurally validated at the
  execution choke point and on creation.
- `shell_mode_` member, toggle, and `/stats` display removed; bare `!`
  prints usage. `allowed_commands` documented informational-only in the
  header; no user-facing text claimed enforcement.
- `is_docker_available` moved public for the routing check; no other
  API changes.
