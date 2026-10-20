# The agent tool-call loop: design

Status: draft for review. Docs only; no runtime code lands under this
document. Nothing described here is implemented yet unless a separate
PR does it.

## Why this document exists

Today Agentware is a user-driven tool system: the user types tool
commands (`search:`, `read:`, `write:`, `replace:`, `grep:`, `git:`,
`github:`, `cmd:`, `!`, `/fetch`, ...) and the agent executes them. The
model summarizes what already happened. See the system prompt in
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
  instructions. The same boundary `classify_input` already enforces
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
  return is **treated as data, never as instructions**. This includes
  model-side: observations from these tools are fed back as quoted,
  non-executable context only (the assistant-output-only rule applies
  to them symmetrically). Fetched and search content is stored in
  memory as quoted data with a clear untrusted-data marker; it is never
  promoted to instructions, system context, or tool input.
- **Bash defaults to sandbox.** Model-initiated `cmd:` dispatches
  through the sandbox first exactly like a user-initiated command;
  the existing host-with-consent fallback applies.

## The tool registry (preceded PR-3 by design)

A single source of truth: name, description, parameter schema, risk
tier (`read-only` | `mutating`), handler. Two artifacts are generated
from it so they cannot drift:

1. the dispatch surface (user prefix routing stays as-is, so
   user-facing syntax and pipe-mode output are preserved), and
2. the tool section of the system prompt in `src/ai_service.cpp`,
   replacing the hand-maintained for list.

`list:` (read-only directory/file listing with glob support, built on
the native walk in `search_service.cpp`) is the first registry-native
addition.

## Tool call protocol

Recognition rule: a tool call is recognized only when the entire
assistant message is a single JSON object, or when the message contains
exactly one tagged fenced code block whose body is a single JSON
object. JSON that appears inside prose, examples, or quoted snippets
never executes. The parser validates the whole message shape first and
rejects anything that is not one of those two forms before looking at
fields.

The assistant emits exactly one JSON object, either bare or in the
first fenced code block of the reply:

```json
{"tool": "grep", "arguments": {"pattern": "handle_", "directory": "src"}}
```

- `tool` must match a registry name; unknown or malformed calls return
  a recoverable system message: `unknown tool X; available: ...`.
- Malformed JSON, missing fields, or extra calls in one reply are
  rejected with the same recoverable framing. The observation names
  the problem and lists valid shapes.
- Cases the user asked to gate on (from the routing audit): malformed
  JSON, unknown tool, and multi-call replies become known-input golden
  tests in `src/tests/run_routing_golden.sh` with canned mock-provider
  outputs once the seam exists.

### Completion signal

A reply that contains no tool call is treated as completion only if it
also carries no pending work. To make completion explicit (and to let
the UI distinguish "done" from "waiting on the user"), the assistant may
emit a completion signal instead of a tool call:

```json
{"done": true, "summary": "Found 3 callers of handle_ in src/."}
```

or, when blocked on the user:

```json
{"needs_user_input": "Which of the two matching files should I edit?"}
```

- `done` ends the loop and records the summary; the model may still
  narrate in the same reply.
- `needs_user_input` ends the loop and surfaces the question in the UI
  (and in pipe mode on stdout); the next user prompt resumes a fresh
  loop with the prior trace available as context.
- A bare reply with neither a tool call nor a signal is treated as
  `done` with an empty summary (current behavior, preserved for
  backwards compatibility).
- Signals are parsed from assistant output only, under the same
  assistant-output-only rule as tool calls.

## Error and recovery budget

- Unknown tool: reply with available names; do not execute anything.
- Validation failure (path, pattern, args): observation states the
  validator's message verbatim; the model may retry within the cap.
- Cancelled consents: observation says so; no partial state.
- Per-tool timeout: each tool call runs under a fixed timeout (default
  30s, configurable). When it trips, the tool is terminated, the
  partial output captured so far is returned as the observation, and the
  model is told `tool timed out after Ns; partial output above`. The
  model may retry or move on within the cap.
- Total observation budget: the cumulative size of all observations in
  one user request is capped (default 64 KiB). When the budget trips,
  further tool calls are refused with `observation budget exhausted;
  summarize what you have`, and the loop ends with a final summary.
- The hard turn cap ends even healthy loops, with a final summary
  from the model; the UI renders the trace exactly like manual tool
  use (`Tool(args)` + result blocks), so engineering-loop output
  stays inspectable in context.

## Policies decided (2026-10-20)

Model-initiated writes are **cwd-only** (approved): relative paths
must resolve under the current working directory (the existing
`Validator::validate_file_access` rule), absolute paths are refused
for model-initiated writes even though user-initiated tools accept
them outside cwd. The check operates on **canonical paths**: symlinks
are resolved and `..` segments are collapsed before the containment
comparison, so `../sibling`, `/tmp/link -> /usr/bin`, and other
aliases cannot launder a write outside cwd. Write-capable tools enter
the model-callable registry only with this policy implemented in the
model-call path.

Memory fact capture is **consent-gated in v1** (approved):
`remember:` from the model presents the fact and asks `y/N` before
persisting. It is mutating-tier for the model-call surface even
though the user-facing command stays ungated as today.

Resolved alongside: turn cap = 8 per user request, configurable;
`fetch:` / `search:` auto-run as read-only with their results treated
as untrusted data.

Consent and TTY policy (approved): mutating tools on the model-call
surface reuse the existing consent gate (`y/N`). When stdin is not a
TTY, mutating tools are **denied** rather than prompted; the loop
never reads consent answers from piped chat input, so a non-interactive
run cannot be driven into a mutating action by crafted input. The
denial is returned to the model as an observation
(`consent denied: no TTY`) and counts as a turn. Read-only tools are
unaffected and auto-run in non-interactive mode.

## Plan step

Before issuing tool calls, the model states a short plan. The plan is
rendered in the UI as a distinct block so the engineering loop stays
inspectable in context, exactly like manual tool use.

Protocol: on the first turn of a loop (or whenever the model revises
its approach), the assistant may include a `plan` field:

```json
{"plan": ["grep handle_ in src", "read the top caller", "propose the edit"]}
```

- The plan is optional but encouraged for multi-step tasks; a single
  tool call with no plan is allowed.
- The UI renders the plan as a numbered list above the tool calls of
  that turn; pipe mode emits it as a `Plan:` block on stdout.
- A revised plan replaces the previous one in the trace (no stale
  plans accumulate).
- The plan is data for the user, not instructions to the runtime: it
  never triggers tool execution on its own.
- This is the last piece of the loop (PR 4 in the breakdown below) and
  ships behind the same flag.

## Test plan

- The routing audit is golden coverage (`run_routing_golden.sh`, 35
  cases): user-side cases run through the real binary today; the three
  model-side cases (malformed JSON, unknown tool, multi-call) activate
  with canned mock-provider outputs once the seam exists. A
  **symlink-escape case** (a symlink inside cwd resolving outside it,
  whose write must be refused) is added to the golden suite alongside
  the model-side cases when the canonical-path check lands.
- Existing guarantees pinned: layout-contract gtests, pipe-mode output
  unchanged, `remediation_probes.sh`, `has_prefix_ci` unit tests,
  clang-format cleanliness (`--dry-run -Werror`).

## PR breakdown

Each step ships behind the flag `AGENTWARE_TOOL_CALL_LOOP` (default
off). With the flag off, no reply is parsed and behavior is exactly
today's. Merge order is (1) -> (2) -> (3) -> (4).

1. **Registry + `list:`** (merged). `Core::ToolRegistry` / `ToolSpec`,
   the generated system-prompt tool section in `ai_service.cpp`,
   `known_names()` recovery, the `list:path[:glob]` dispatch branch,
   and the golden suite at 35 cases. No loop yet.
2. **Loop with read-only tools.** Parse one JSON call per reply, run
   read-only tools automatically, feed truncated observations back,
   enforce the turn cap, handle unknown tool / malformed JSON /
   multi-call with the `unknown tool X; available: ...` framing. No
   mutating tools callable by the model.
3. **Mutating tools.** Mutating-tier tools enter the model-callable
   registry behind the existing consent gate (`y/N`), with cwd-only
   writes (canonical paths, symlinks resolved, `..` collapsed) and the
   symlink-escape refusal. `remember:` is consent-gated in v1.
4. **Plan step.** Plan rendering in the UI and pipe mode, plus the
   `done` / `needs-user-input` completion signals, and the deferred
   model-side golden tests (malformed JSON, unknown tool, multi-call,
   symlink escape) activated with canned mock-provider outputs.

## Decisions record (all approved 2026-10-20)

1. **Turn cap**: 8 tool turns per user request, configurable.
2. **Model-initiated writes**: cwd-only; absolute paths refused on
   the model-call surface.
3. **`fetch:` / `search:`**: read-only tier, auto-run permitted,
   results treated strictly as untrusted data.
4. **`remember:`**: consent-gated from the model in v1 (the
   user-facing command behavior is unchanged).
5. **cwd-only enforcement detail**: the containment check compares
   canonical paths (symlinks resolved, `..` collapsed); a
   symlink-escape refusal is on the golden-test list.
