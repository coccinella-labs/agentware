<p align="center">
  <img src="https://raw.githubusercontent.com/coccinella-labs/agentware/main/.github/assets/thumbnail.png" alt="agentware" width="100%">
</p>

# agentware

A C++20 AI coding agent that runs in your terminal. It talks to hosted model
providers, reads and edits files through its own commands, and works offline
against local models.

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

## What it does

Around twenty services behind one binary: agent orchestration, AI provider calls, auth, checkpoints, codebase search, command execution, configuration, context tracking, a database layer, file and multi-file operations, git and GitHub integration, an MCP client module, memory management, sandboxing, theming, input validation, and a web service.

Codebase search shells out to `rg` and `fd` when they are on `PATH` and falls
back to a built-in scanner when they are not, so search works either way.
`github:threads:owner/repo:pr` reads inline review threads without the GraphQL
API.

Model Context Protocol server entries are configured in
`data/mcp_servers.json`, and two are pre-populated by default: a filesystem
server and a git server, both declared as `npx` commands. The MCP transport is
not implemented, so no server process is started and no MCP tool is called;
the `/mcp` commands report their own configuration and say so.

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

`ctest` runs three targets. `basic_tests` is the unit suite, currently 120 cases
across 38 suites, covering command dispatch and redaction, file-access policy,
project-root discovery, MCP and GitHub availability, review threads, sandbox
construction, spawn, bounded execution, the Docker availability probe, safety,
configuration, results, and Docker behavior, empty replace-pattern rejection,
issue parsing, git log handling, memory persistence, input validation, and both
search backends. `remediation_probes` is a shell harness that drives the built
binary against known-bad input and asserts it refuses. It is POSIX-only and is
not run on the Windows CI job. `routing_golden` is a 35-case shell harness that
pins the dispatch routing contract through the real binary in a hermetic
workspace; it makes zero outbound requests.

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

CI additionally runs a build matrix, coverage, lint, Docker end-to-end, and
release jobs. Nothing in either target exercises the agent against a live
model. The `e2e` job needs Docker; the `coverage` job needs its toolchain
installed.

## Contributing

`CONTRIBUTING.md` is the authority, not this file. External contributions are not accepted at this time; the project is maintained by collaborators. If that changes, the contributing guide will say so first.

## License

MIT. See [LICENSE](LICENSE).
