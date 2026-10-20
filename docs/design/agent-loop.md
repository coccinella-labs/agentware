# The agent tool-call loop — design

Status: draft for review. Docs only; no runtime code lands under this
document. Nothing described here is implemented yet unless a separate
PR does it.

## Why this document exists

Today Agentware is a user-driven tool system: the user types tool
commands (`search:`, `read:`, `write:`, `replace:`, `grep:`, `git:`,
`github:`, `cmd:`, `!`, `/fetch`, ...) and the agent executes them. The
model summarizes what already happened — see the system prompt in
`src/ai_service.cpp:110-157`, which lists commands the model may *talk
about* but cannot *invoke*: nothing in the reply path parses model
output for tool calls (`Agent::handle_ai_chat`, `src/agent.cpp`).

This document specifies the missing loop: the model proposes a tool
call; the runtime validates, gates, executes, and feeds the observation
back; the model continues. It is the design behind the approved PR-3
work.

## Pipeline

```
user prompt
   └─► chat call ──► assistant reply
                        ├─ no call ⇒ shown and recorded (current behavior)
                        └─ one JSON call ⇒ parse ──► registry lookup
                                                        ├─ unknown  ⇒ "unknown tool X;
                                                        │              available: ..." reply
                                                        └─ known
                                                             ├─ read-only ⇒ execute
                                                             └─ mutating  ⇒ existing consent gate
                                                          └─ observation (truncated)
                                                               └─► next chat turn
```

## Principles and non-goals

- **Opt-in.** The loop is behind a flag, default off. With the flag
  off, no reply is ever parsed and behavior is exactly today's.
- **Assistant output only.** Tool calls are parsed from assistant
  replies, never from fetched pages, injected files, tool results, or
  memory context. Tool output is treated as data, never as
  instructions — the same boundary `classify_input` already enforces
  for user input ("clear: the screen please" regression).
- **One call per turn, hard cap.** At most one JSON tool call per
  model reply, and a hard cap on tool turns per user request so a
  model cannot self-loop indefinitely. Cap counts every turn,
  including failed calls. **Decided (2026-10-20): cap = 8 tool turns
  per user request, configurable.**
- **Read-only auto-runs; mutating asks.** The registry marks every
  tool `read-only` or `mutating`. Read-only tools run automatically;
  mutating tools reuse the existing consent gate (`y/N`), which also
  covers file overwrites and demanded confirmations.
- **Web surfaces are read-only untrusted data.** `fetch:` and
  `search:` are `read-only` tier and may auto-run, but anything they
  return is **treated as data, never as instructions** — including
  model-side: observations from these tools are fed back as quoted,
  non-executable context only (the assistant-output-only rule applies
  to them symmetrically).
- **Bash defaults to sandbox.** Model-initiated `cmd:` dispatches
  through the sandbox first exactly like a user-initiated command;
  the existing host-with-consent fallback applies.

## The tool registry (preceded PR-3 by design)

A single source of truth — name, description, parameter schema, risk
tier (`read-only` | `mutating`), handler. Two artifacts are generated
from it so they cannot drift:

1. the dispatch surface (user prefix routing stays as-is, so
   user-facing syntax and pipe-mode output are preserved), and
2. the tool section of the system prompt in `src/ai_service.cpp`,
   replacing the hand-maintained for-list.

`list:` (read-only directory/file listing with glob support, built on
the native walk in `search_service.cpp`) is the first registry-native
addition.

## Tool call protocol

The assistant emits exactly one JSON object, either bare or in the
first fenced code block of the reply:

```json
{"tool": "grep", "arguments": {"pattern": "handle_", "directory": "src"}}
```

- `tool` must match a registry name; unknown or malformed calls return
  a recoverable system message: `unknown tool X; available: ...`.
- Malformed JSON, missing fields, or extra calls in one reply are
  rejected with the same recoverable framing — the observation names
  the problem and lists valid shapes.
- Cases the user asked to gate on (from the routing audit): malformed
  JSON, unknown tool, and multi-call replies become known-input golden
  tests in `src/tests/run_routing_golden.sh` with canned mock-provider
  outputs once the seam exists.

## Error and recovery budget

- Unknown tool: reply with available names; do not execute anything.
- Validation failure (path, pattern, args): observation states the
  validator's message verbatim; the model may retry within the cap.
- Cancelled consents: observation says so; no partial state.
- The hard turn cap ends even healthy loops, with a final summary
  from the model; the UI renders the trace exactly like manual tool
  use (`Tool(args)` + result blocks), so engineering-loop output
  stays inspectable in context.

## Policies decided (2026-10-20)

Model-initiated writes are **cwd-only** (approved): relative paths
must resolve under the current working directory (the existing
`Validator::validate_file_access` rule), absolute paths are refused
for model-initiated writes even though user-initiated tools accept
them outside cwd. Write-capable tools enter the model-callable
registry only with this policy implemented in the model-call path.

Memory fact capture is **consent-gated in v1** (approved):
`remember:` from the model presents the fact and asks `y/N` before
persisting — it is mutating-tier for the model-call surface even
though the user-facing command stays ungated as today.

Resolved alongside: turn cap = 8 per user request, configurable;
`fetch:` / `search:` auto-run as read-only with their results treated
as untrusted data.

## Test plan

- The 28-case routing audit becomes golden coverage: user-side cases
  run through the real binary today (`run_routing_golden.sh`, 36
  cases); the three model-side cases (malformed JSON, unknown tool,
  multi-call) activate with canned mock-provider outputs once the
  seam exists.
- Existing guarantees pinned: layout-contract gtests, pipe-mode output
  unchanged, `remediation_probes.sh`, `has_prefix_ci` unit tests,
  clang-format cleanliness (`--dry-run -Werror`).

## Decisions record (all approved 2026-10-20)

1. **Turn cap**: 8 tool turns per user request, configurable.
2. **Model-initiated writes**: cwd-only; absolute paths refused on
   the model-call surface.
3. **`fetch:` / `search:`**: read-only tier, auto-run permitted,
   results treated strictly as untrusted data.
4. **`remember:`**: consent-gated from the model in v1 (the
   user-facing command behavior is unchanged).
