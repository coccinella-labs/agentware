# Roadmap proposal

Evidence for every claim lives in `docs/audit/current-state.md` and
`docs/audit/gaps-and-risks.md`. No code was changed for this proposal.

## Options considered

1. **Preserve and finish.** Keep the design, complete MCP transport, the
   database feature, and all providers. Rejected: it preserves the two
   largest unknowns (MCP transport, PostgreSQL purpose) and the tripled
   release flow without adding the missing test net first.
2. **Simplify.** Cut MCP simulation, database stubs, and marginal
   providers; keep chat, files, shell, git, checkpoints, memory. Viable
   fallback if maintainers disagree on G-05/G-08, but it discards working
   provider breadth users already rely on.
3. **Redesign selectively (recommended).** Keep the working REPL,
   providers, file/shell/git/web tools, checkpoints, memory, sandboxes,
   and themes. Fix crash and trust bugs, put one release path and one
   install path in charge, resolve the four undecided questions, and add
   the missing behavioral tests before touching anything else.
4. **Reposition or archive.** Not warranted: CI is green, the binary
   builds and runs on three OSes, E2E passes over mocks, and commits
   continue. Dormancy would waste a working agent.

## Recommended direction

Selective redesign: stabilize foundations, then decide MCP and database on
evidence, not suspicion. Nothing here assumes a rewrite; the architecture
in `docs/audit/architecture.md` stays.

## Phase 0 — Safety net (first, blocks everything else)

Deliverables: compiled tests for `FileService`, `Validator`,
`CommandService` denylist/timeout, `MemoryManager` round-trip, AI payload
and response parsing per provider (mocked transport), checkpoint
create/restore round-trip; E2E extended with one negative case per
provider, one offline Ollama case, and fail-closed behavior; Codecov and
Windows preflight made blocking.

Acceptance: `ctest` covers every `Services::*` used by the REPL; a
deliberately reintroduced G-02/G-03 bug fails CI. Verification: `cmake
--build build && ctest --output-on-failure` plus the E2E compose run.

## Phase 1 — Crash and trust fixes

Deliverables: guard `stoi` parsing, null-check `ai_service_` on
`/compress` and `/fetch`, print and save `/fetch` text results, route
file commands through `is_safe_path`, escape git shell paths, scope
sandbox volume mounts, pick one search backend and stop sending the
unused key.

Acceptance: scripted probes for each G-01/G-02/G-03/G-04/G-06 case exit
cleanly with an error message instead of crashing or leaking. Depends on
Phase 0 for regression proof.

## Phase 2 — One release path

Deliverables: single canonical flow (recommend label-driven bump,
tag, build, sign, publish), one install path, corrected CPack deps,
working `prepare-release.sh`, real CHANGELOG entries, repaired
`--version`/`HEALTHCHECK`, merged or closed `version-bump-0.1.7`.

Acceptance: tagging `v0.x.y` produces exactly one GH release with
matching notes, one GHCR image that reports healthy, and `agentware-agent
--version` printing the tag. Depends on Phase 0 for the build matrix.

## Phase 3 — Decide MCP and database

Deliverables: either a real MCP transport with a passing tool-call test
and documented servers, or removal of the simulated surface and
`data/mcp_servers.json`; either a first real `DatabaseService` caller
with integration coverage, or removal of the stub, its `DB_*` vars, and
its per-OS quoting fork.

Acceptance: no simulated success messages remain, and no shipped code
path returns hardcoded success. Depends on Phases 0-1 so the decision is
observed, not guessed.

## Phase 4 — Docs and hygiene (optional, cheap)

Deliverables: README usage and command reference, restored setup and
architecture notes (this audit's `architecture.md` is the seed),
corrected `SECURITY.md` range, removed dead metrics and orphans
(`token_usage_`, `should_skip_file`, `safe_get_string`, `mock_ollama.py`),
pinned remaining installs, reproducible-version build.

Acceptance: a new contributor builds, runs, and exercises every command
from docs alone.

## Next three actions

1. Land Phase 0: add the service-level tests and make coverage and E2E
   blocking (closes G-10, unblocks the rest).
2. Land Phase 1 crash fixes: `stoi` guards, `ai_service_` null checks,
   `/fetch` text output (closes G-02, G-03).
3. Resolve the four undecided questions in Gaps (MCP, database, search
   backend, release flow) so Phase 2 and Phase 3 have owners and dates.
