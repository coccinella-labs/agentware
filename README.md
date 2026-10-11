<p align="center">
  <img src="https://raw.githubusercontent.com/coccinella-labs/agentware/main/.github/assets/thumbnail.png" alt="agentware" width="100%">
</p>

# agentware

A C++20 AI coding agent that runs in your terminal. It talks to hosted model providers, reads and edits code through MCP filesystem and git servers, and works offline against local models.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/agentware-agent
```

On first run it prints a welcome screen and asks for a mode: online against a hosted provider, or offline against a local model.

## Providers

Ten agent modes, one per provider or model family: Together, Llama 3B, Cerebras, Llama Latest, Llama 3.1, Fireworks, Groq, DeepSeek, and OpenAI. Each needs its API key in the environment; see `.env.example` for the full list. No key is needed for offline mode.

## What it does

Around twenty services behind one binary: agent orchestration, AI provider calls, auth, checkpoints, codebase search, command execution, configuration, context tracking, a database layer, file and multi-file operations, git and GitHub integration, MCP client support, memory management, sandboxing, theming, input validation, and a web service.

Model Context Protocol servers are configured in `data/mcp_servers.json`. Two ship by default, both run through `npx`: a filesystem server rooted at the working directory and a git server.

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

The suite is two tests: a framework sanity check and a version-string check. That is the honest extent of it. CI additionally runs build-matrix, coverage, lint, Docker end-to-end, and release jobs, but nothing here exercises the agent's behavior. The `e2e` job needs Docker; the `coverage` job needs its toolchain installed.

## Contributing

`CONTRIBUTING.md` is the authority, not this file. External contributions are not accepted at this time; the project is maintained by collaborators. If that changes, the contributing guide will say so first.

## License

MIT. See [LICENSE](LICENSE).
