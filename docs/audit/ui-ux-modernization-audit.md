# UI/UX modernization and presentation-layer audit

This is a separate workstream from feature correctness. It recommends
progressive, backward-compatible modernization of the terminal REPL; it does
not propose a TUI framework, command syntax changes, animations, or code
changes.

## Status note (2026-10-12)

The baseline below is preserved as observed. Two of its P1/P2 rows are superseded
by committed work in the current tree (`f908f2bf` for P1-1,
`bdf33b2b` for P1-2 and P1-3):

- **"ANSI is unconditional"**: **implemented (P1-1).** The theme path no longer
  bypasses TTY detection and `NO_COLOR`.
- **"No responsive layout system ... no terminal-width measurement, wrapping
  strategy, column truncation or continuation indentation"**: **implemented
  (P1-3).** Width comes from `ioctl(TIOCGWINSZ)` with an 80-column fallback, and
  wrapping uses continuation indentation.
- **"Startup repeats commands"**: **implemented (P1-2).** Zero duplicate
  quick-help tokens.

Still open: the truthful-runtime-status row (`Memory=Active` /
`Commands=Available` remain unprobed), the noisy-information-hierarchy row, and
every Windows question. See `agent-ui-audit.md`.

## Baseline inspected

Presentation surfaces: logo/startup, mode and provider prompts, prompt,
quick help, `help`, `/help`, `/tools`, `/stats`, command confirmation,
spinner, all handler output, `UI` success/warning/error functions, sandbox,
auth and MCP status. Representative REPL captures were run on macOS arm64 at
`COLUMNS=40` and `COLUMNS=60`, with `TERM=dumb NO_COLOR=1` for one capture.

The process emitted ANSI sequences even with `NO_COLOR=1` and made no visible
use of `COLUMNS`. The narrow capture showed long unwrapped command examples,
two overlapping quick-help rows, and two Ready messages. It is evidence of
missing adaptation, not a claim about every terminal emulator.

## Findings

| Priority | Finding | Evidence and impact |
|---|---|---|
| P1 | Help is inaccurate and fragmented | `help`, `/help`, and `/tools` are three manual inventories. Main help advertises nonexistent `/save`, `/resume`, `/sessions`, `/checkpoints`, and shell toggling; actual dispatch differs. Discoverability is actively harmful. |
| P1 | ANSI is unconditional | `ui.cpp` prints raw ANSI constants; no color capability, `NO_COLOR`, or non-TTY policy. Logs/pipes, screen readers, Windows variants, light themes and captured output are degraded. |
| P1 | Runtime status is not truthful enough | Startup `Memory=Active`/`Commands=Available` have no health check. `/stats` reports an incorrect model name for Fireworks, Groq, DeepSeek and OpenAI. MCP/Auth need careful uncertainty language. |
| P1 | Command risk feedback is inconsistent | `cmd:`/`!` warn then route; `/sandbox run` displays another shape. The user cannot see a compact, consistent plan of backend, workspace mount, network, confirmation and outcome. |
| P2 | No responsive layout system | Fixed spaces, ASCII divider width, long path/output and table-like listings have no terminal-width measurement, wrapping strategy, column truncation or continuation indentation. |
| P2 | Presentation is duplicated | `UI` owns some colored messages, but `Agent` has dozens of bespoke `cout` formats. No reusable command header, key/value list, result, diagnostic, progress, or table primitive exists. |
| P2 | Interaction states are incomplete | Spinner has no operation label, elapsed time, cancellation instruction or non-TTY fallback. Timeouts/partial results are service-specific prose. There is no explicit cancelled/partial/degraded result vocabulary. |
| P2 | Information hierarchy is noisy | Startup repeats commands and Ready states; the large logo competes with the first action. Help uses many categories but hides actual syntax and important state/dependency prerequisites. |
| P2 | Keyboard/recovery flow is weak | stdin supports normal line editing supplied by the terminal, but there is no documented interrupt behavior, contextual invalid-argument help, command completion, or reliable “what next” recovery text. |
| P3 | Unicode/locale/platform output unverified | Logo uses Unicode block glyphs; no fallback was found. Windows uses platform process abstractions but visual output, terminal code page, and ANSI support were not exercised. |

## Lightweight design-system proposal

Keep stdout REPL and introduce a small presentation boundary, not a framework:

1. **Capabilities registry:** one metadata source for command syntax,
   aliases, availability/dependency notes, and help grouping. Render `help`,
   `/help`, `/tools`, and invalid-command suggestions from it.
2. **Terminal profile:** detect TTY, width and color support; honour `NO_COLOR`
   and an explicit plain/colour choice. Use ASCII logo/fallback when Unicode is
   not appropriate.
3. **Primitives:** `section`, `key_value`, `command_usage`, `notice`,
   `progress`, `result`, and width-aware list/table rendering. Presentation
   primitives must render plain text identically in meaning.
4. **Result states:** standardize `success`, `failed`, `cancelled`,
   `timed_out`, `partial`, and `unavailable`, each with the operation,
   backend when relevant, and a recovery action.

These are implementation seams, not visual styling requirements. Preserve
existing command grammar and retain a dense, copyable text mode.

## Representative redesign proposals

### Startup (same commands; less noise)

Before: logo, two duplicated quick-help rows, two Ready messages, and
unverified green status claims.

After (plain-text mode):

```
Agentware  |  Offline: llama3.2:3b
Type a question, `help`, or `/help`.  `exit` quits.
>
```

Optional detail should be requested by `/status`, where every entry reports
`available`, `unavailable`, or `not verified` with its dependency.

### Risky execution (same confirmation semantics)

```
Run: git status
Backend: Docker sandbox (default) | network: off | workspace: read/write
Continue? [y/N]
```

The finished state must report sandbox/host, exit code, duration, truncation
and timeout explicitly. When Docker is absent, it must say host execution is
unisolated before the existing opt-in.

### Failure and recovery

```
error: cannot fetch http://127.0.0.1:18321/big.txt
reason: connection refused
next: check the URL or run `/fetch <url> raw` for response details
```

The error code/technical detail stays available but should not replace the
operation and recovery message.

## Accessibility and compatibility test matrix

Required before visual changes are accepted:

| Scenario | Acceptance criterion |
|---|---|
| 40, 80, 120 columns | No lost meaning; long paths wrap/truncate with continuation; headings and examples remain readable. |
| TTY and piped output | No ANSI in plain/piped/`NO_COLOR` output; semantic content is the same. |
| Light/dark themes | No meaning depends on colour; colours meet readable contrast where terminal palette permits. |
| Unicode unavailable | ASCII-safe logo and indicators render without mojibake. |
| macOS, Linux, Windows | Identical command grammar and result-state labels; platform-specific process errors are normalized. |
| Keyboard / interrupts | Document and test empty input, EOF, Ctrl-C behavior during prompt and a long operation. |

Snapshot tests should normalize volatile values (paths, timings, colours) and
assert semantic output for startup, help, warning confirmation, sandbox
unavailable, timeout, partial/truncated result, and plain output. Add a
help-registry contract test so a displayed command cannot drift from dispatch.
