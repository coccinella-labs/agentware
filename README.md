<p align="center">
  <img src="https://raw.githubusercontent.com/coccinella-labs/agentware/main/.github/assets/thumbnail.png" alt="agentware" width="100%">
</p>

# agentware

A C++20 AI coding agent that runs in your terminal. It talks to hosted model
providers, reads and edits files through its own commands, runs an autonomous
tool-call loop, and works offline against local models.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/agentware-agent
```

On first run it prints a welcome screen and asks for a mode: online against a hosted provider, or offline against a local model.

## Providers

Nine agent modes, one per provider or model family: Together, Llama 3B,
Cerebras, Llama Latest, Llama 3.1, Fireworks, Groq, DeepSeek, and OpenAI.
Each hosted mode needs its API key in the environment; see `.env.example` for
the list. The three Llama modes and Offline run against a local Ollama server
and need no key.

An experimental adapter can route local Ollama requests through an installed
xoi CMake package:

```bash
cmake -S . -B build -DAGENTWARE_USE_XOI_OLLAMA=ON \
  -DCMAKE_PREFIX_PATH=/path/to/xoi/install
```

The option is off by default and does not affect hosted providers. Agentware
continues to own its prompt, tool loop, permissions, and safety checks; this
adapter is a prototype, not a production-readiness claim.

## What it does

Around twenty services behind one binary: agent orchestration, AI provider calls, auth, checkpoints, codebase search, command execution, configuration, context tracking, a database layer, file and multi-file operations, git and GitHub integration, an MCP client module, memory management, sandboxing, theming, input validation, and a web service.

A central tool registry classifies every tool by capability (read-only or
mutating), model-surface availability, and interactive consent requirements,
providing case-insensitive prefix resolution and canonical tool-call parsing.

Codebase search shells out to `rg` and `fd` when they are on `PATH` and falls
back to a built-in scanner when they are not, so search works either way.
`github:threads:owner/repo:pr` reads inline review threads without the GraphQL
API.

Model Context Protocol server entries are configured in
`data/mcp_servers.json`, and two are pre-populated by default: a filesystem
server and a git server, both declared as `npx` commands. The MCP transport is
not implemented, so no server process is started and no MCP tool is called;
the `/mcp` commands report their own configuration and say so.

## Model-driven tool loop (autonomous mode)

Set `AGENTWARE_TOOL_CALL_LOOP=1` to allow the model to autonomously invoke
tools across multiple turns. By default the loop is off (`0`), where the
agent operates as a direct conversational assistant executing user-typed
commands.

When enabled, assistant responses are parsed for JSON tool calls (e.g.
`{"tool": "read", "arguments": {"filename": "src/main.cpp"}}`). The runtime
validates each call through an execution gate before running it, captures the
output, and feeds truncated observations back to the model until it outputs a
final text answer or reaches turn bounds.

Strict safety invariants are enforced on the model surface:

- **Read-only execution:** Mutating tools (`cmd:`, `write:`, `replace:`,
  `remember:`, `clear`, `forget`) are strictly refused on the model surface.
- **Model-disabled tools:** Heavy tools (`todos:`, `analyze:`, `git:diff`) are
  refused on the model surface.
- **CWD containment:** Paths that escape the current working directory via
  parent traversal (`..`), absolute paths, or external symlinks are refused.
- **Secrets filtering:** Sensitive paths (`.env`, `.ssh`, tokens, secret keys)
  are blocked from reads and filtered out of search results.
- **Consent gates:** Outbound network tools (`fetch:`, `github:`) require
  interactive TTY confirmation and fail with `consent denied: no TTY` in
  unattended or non-interactive environments.

| Variable | Default | Description |
|---|---|---|
| `AGENTWARE_TOOL_CALL_LOOP` | `0` | Set to `1` or `true` to enable the autonomous tool loop |
| `AGENTWARE_TOOL_TURN_CAP` | `8` | Maximum autonomous turns before returning control to the user |
| `AGENTWARE_TOOL_TIMEOUT_SEC` | `30` | Per-tool execution timeout in seconds |
| `AGENTWARE_NET_TIMEOUT_SEC` | `10` | Network timeout for model requests during tool loop turns (30s elsewhere) |

## Modal line editing (optional)

Set `AGENTWARE_VIM_MODE=1` to edit the REPL prompt with Vim-style keys
instead of plain `std::getline`. The default is off, so nothing changes until
you opt in. Piped and redirected stdin always uses plain line input, so
scripts and CI are unaffected even with the variable set.

Insert mode is the usual one: characters insert, Backspace deletes. These
keys work in **normal** mode, reached with `i`:

| Key | Action |
|---|---|
| `i` | back to insert mode |
| `h` | cursor left |
| `l` | cursor right |
| `0` | cursor to start of line |
| `$` | cursor to end of line |
| `x` | delete the character under the cursor |

Enter submits through the existing REPL flow. Anything else in normal mode is
a silent no-op rather than an error.

Two limits worth knowing before you enable it:

- **Arrow keys, function keys, Home and End do nothing.** Escape sequences
  are consumed and ignored in this version, so there is no `j`/`k` movement
  and no history navigation.
- **Windows key identity is unverified.** The console mapping handles the
  unmapped keys that arrows and function keys produce, but nothing
  distinguishes one special key from another on that platform.

## Scope

`src/` holds the implementation, public headers under `src/include/`, tests under `src/tests/`, and automation under `src/scripts/`. `CMakeLists.txt` and `Makefile` both build the same targets; `make` wraps the CMake invocation.

## Releases

Merging to `main` bumps the version, tags it, and creates a GitHub release
automatically. Binaries are attached by a separate build workflow that runs on
the tag push.

That separation has one requirement worth knowing: tags and releases created
with `GITHUB_TOKEN` do not trigger new workflow runs, so the build never sees
them and the release stays empty. The version-bump job therefore prefers a
`RELEASE_TOKEN` secret (a personal access token with contents write access)
and falls back to `GITHUB_TOKEN` with a warning. Without that secret, every
automated release ships with no binaries, which is what happened to the
releases that prompted this note.

To backfill a release, run the Release workflow by hand against its tag from
the Actions tab.

## Tests

```bash
cmake --build build --target test
```

`ctest` runs these targets:

- **`basic_tests`**: Google Test suite with 130 cases across 42 suites. Covers
  command dispatch and redaction, file-access policy, project-root discovery,
  MCP and GitHub availability, review threads, sandbox construction, bounded
  execution, safety denylists, memory persistence, input validation, search
  backends, the central tool registry and snapshot verification, tool execution
  gating, and process timeout utilities.
- **`remediation_probes`**: A POSIX shell harness driving the built binary
  against known-bad input and asserting refusal. It is POSIX-only and is not
  run on the Windows CI job.
- **`routing_golden`**: A 35-case shell harness pinning command dispatch and
  routing contracts through the built binary in a hermetic workspace with zero
  outbound requests.
- **`tool_loop_golden`**: A 131-case shell harness exercising the model-driven
  tool-call loop against a hermetic mock provider (`mock_tool_provider.py`).
  Verifies JSON parsing, turn caps, mutating tool refusals, model-disabled
  commands, path containment, secrets protection, variant sweeps, and consent
  denials.
- **`provider_readiness`**: The wait the suite above uses to decide that a mock
  provider is up. Pins prompt readiness, that the server does not resolve its
  own bind address, that a bind error is still a bind error, and that a
  provider which genuinely fails to start fails fast with a reason instead of
  spending a deadline in silence.

### Sandbox Tests

Seven sandbox test areas exercise Docker-isolated command execution:

| Test Suite | Description |
| --- | --- |
| `SandboxCommandTest` | Shell argument parsing and hostile value handling |
| `SandboxSpawnTest` | Process spawn, exit codes, missing binary handling |
| `SandboxBoundedTest` | Timeout enforcement, hang detection, bounded execution |
| `SandboxSafetyTest` | Denylist filtering (`.` substring), dangerous command blocking |
| `SandboxConfigTest` | Config validation: sane configs accepted, hostile rejected |
| `SandboxResultTest` | Result formatting: success, empty, failure, timeout |
| `SandboxDockerTest` | End-to-end fake-docker routing: success, daemon failure, hang timeout |

CI additionally runs a build matrix (Ubuntu, macOS, Windows), coverage, linting,
Docker end-to-end, and release jobs. The `e2e` job needs Docker; the `coverage`
job needs its toolchain installed.

## Contributing

`CONTRIBUTING.md` is the authority, not this file. External contributions are not accepted at this time; the project is maintained by collaborators. If that changes, the contributing guide will say so first.

## License

MIT. See [LICENSE](LICENSE).
