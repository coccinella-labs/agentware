# Agentware: UI Improvement Proposal

Presentation improvements derived from `agent-ui-audit.md`. **P1-1, P1-2, and
P1-3 are implemented, measured, and committed**: P1-1 in `f908f2bf`, P1-2 and P1-3
in `bdf33b2b`. **P2-4 and P2-5 are not implemented.** No estimates are attached
to anything here, and no item below was verified by anyone other than the
implementing agent. `851182a5` is the sandbox lifecycle fix, not a UI baseline;
the UI baseline is `bdf33b2b`.

**Provenance.** Every figure attributed to implemented work below was measured by
the implementing agent from the working tree and reviewed as a report. The
reviewer did not independently re-run the build or the tests. All of it is
reproducible from the working tree and disappears if the changes are reverted.

The proposal is deliberately constrained to presentation. It does not reopen
the behavioral work closed in `agent-ux-audit.md`, and it does not attempt to
compensate for a behavioral defect with a cosmetic change.

## Status

| ID | State | Measured result |
| --- | --- | --- |
| P1-1 | Implemented in `f908f2bf` | 8/8 color-policy conditions; `ColorPolicyTest` |
| P1-2 | Implemented in `bdf33b2b` | 0 duplicate quick-help tokens at 40/60/80/120 |
| P1-3 | Implemented in `bdf33b2b` | 11/11 acceptance assertions; 12 `LayoutTest` cases |
| P2-4 | **Not started** | — |
| P2-5 | **Not started** | — |

P1-2 and P1-3 were implemented together, as the sequencing note below predicted:
both depend on how terminal width is obtained. That width now comes from
`ioctl(STDOUT_FILENO, TIOCGWINSZ)` with a validated result and an 80-column
fallback when detection fails. `COLUMNS` is deliberately not used.

## Priorities

### P1-1. Unify the color policy — **implemented and measured**

**Addresses:** U-1.
**Evidence:** the theme path ignores both TTY state and `NO_COLOR`; under
`NO_COLOR=1` exactly one escape sequence survives a session, and it is
`[34mdefault[0m` from `/theme current`.

Proposed change: make `ThemeService` consult the same policy the rest of the
UI already uses, that is, TTY detection plus `NO_COLOR`, rather than `$TERM`
alone. The goal is a single decision point for color, not two that disagree.

Deliberately not proposed:

- Changing which colors are used, or retheming anything. U-1 is a gating
  defect, not a palette defect.
- Retrofitting the themes themselves. A separate audit finding notes that
  theme selection currently has no effect on most UI output; that is a
  behavioral question and belongs with the UX work, not here.

Verification if implemented: the theme output must follow the shared UI color
policy rather than a rule of its own. Across all four conditions in U-1, and
for `TERM=xterm-256color`, `TERM=dumb`, and unset `TERM` where each applies,
both output surfaces must agree: plain when stdout is not a TTY or when
`NO_COLOR` is set, and colored when stdout is a TTY and `NO_COLOR` is unset.

**Outcome:** implemented as proposed. `ThemeService::is_color_supported()`
delegates to `Utils::UI::colors_enabled()`. The criterion above is met, 8 of 8
conditions, including the two rows a faulty capture harness had previously
corrupted. Deliberately **not** done, as scoped: no palette changes, and
`NO_COLOR=""` semantics are untouched (see Open follow-ups).

### P1-2. Fix startup layout — **implemented and measured**

**Addresses:** U-2 and the 136-column startup line in U-5.
**Evidence:** mode and model prompts render on one 136-column line because the
prompts carry no trailing newline; quick help repeats `search:query`,
`cmd:command`, and `read:file`, and its second line opens with `version`.

Proposed change, two parts:

1. Terminate each setup prompt with a newline so prompts and their answers
   occupy separate lines rather than running together.
2. Print quick help once, as a de-duplicated set, grouped so the recommended
   starting commands are distinguishable from alternatives. If the two-line
   form is kept, the continuation must not open with a command that reads as
   part of the previous line.

Not proposed: reducing the amount of help shown. The goal is a clear set, not
necessarily a shorter one.

### P1-3. Make narrow-width output readable — **implemented and measured**

**Addresses:** U-5.
**Evidence:** 39 of 69 `help` lines exceed 40 columns; the status line is 81;
the startup line is 136; alignment is literal space padding with no wrapping
logic, so wrapping separates the description column from its command.

Proposed change: replace literal padding with width-aware layout that derives
the description column from the terminal width, so entries either fit or wrap
predictably with the description still associated with its command. The status
line is the clearest candidate, since a fixed-width key/value line is the
easiest to make responsive.

Not proposed: silently truncating long commands such as
`github:threads:<owner>/<repo>:<pr>[:query]`. Those are needed verbatim, and
truncating a command the user must type is worse than wrapping it.

Verification if implemented: re-measure the width table in U-5 at 40, 60, and
80 columns.

**Outcome:** implemented as proposed. At 40 columns the longest startup line
fell from 136 to 40 with none over the width, and `help` lines over 40 columns
fell from 39 of 69 to 1 of 90. The single exception is
`github:threads:<owner>/<repo>:<pr>[:query]`, the unsplittable token this
proposal predicted would have to exceed the width rather than be truncated. The
truncation guard held: no command syntax is cut at any width.

### P2-4. Reconcile the help screens — **not started**

**Addresses:** U-3.
**Evidence:** three screens, 47 / 37 / 29 lines, with `github:*` and
`!<command>` duplicated between `help` and `/help`, all colon commands
duplicated between `help` and `/tools`, and `/files` documented as
`<path...> [options]` in one place and `<patterns>` in another.

Proposed change: define each screen's audience and stop duplicating between
them, so a user can learn which screen answers which question. One screen for
discovery, one for reference, and no command appearing in two unless the
overlap is deliberate and labelled.

Not proposed: deciding which commands should exist. The audit records that
`/memory` behaves differently from `/tools` documentation and that
`/clear` is absent from `help`. Those are behavioral questions, already
tracked in `agent-ux-audit.md`.

### P2-5. Standardize output and error guidance — **not started**

**Addresses:** U-4.
**Evidence:** indentation varies by command, and `/theme current` indents its
title but not its own description; the `[OK]`/`[WARN]`/`[ERROR]` helpers are
unused by command output; meta-command errors echo the input and offer a next
step while direct-command errors emit a bare `Unknown command`.

Proposed change, in order of value:

1. Give unrecognized direct commands the same guidance the meta path already
   provides: echo what was received and point at the right help screen.
2. Apply one indentation convention across command output.
3. Route user-facing status through the existing shared helpers so a status
   looks the same wherever it appears.

The first is the highest value and the smallest change. It closes the gap
between two error styles that already exist in the same binary.

Not proposed: changing error text that is already accurate, such as the Docker
status messages, which distinguish their failure modes correctly.

## Summary

| ID | Change | Addresses | Priority | State |
| --- | --- | --- | --- | --- |
| P1-1 | Single color policy honoring TTY and `NO_COLOR` | U-1 | P1 | Implemented, `f908f2bf` |
| P1-2 | Startup prompt and quick-help layout | U-2, U-5 | P1 | Implemented, `bdf33b2b` |
| P1-3 | Width-aware output replacing manual alignment | U-5 | P1 | Implemented, `bdf33b2b` |
| P2-4 | Reconcile help screens and `/files` syntax | U-3 | P2 | Not started |
| P2-5 | Consistent indentation, status helpers, error guidance | U-4 | P2 | Not started |

## Suggested sequencing (as planned, before implementation)

P1-1 is self-contained and independently verifiable through the U-1 matrix, so
it can proceed alone. P1-2 and P1-3 both touch layout and are best implemented
together, since both depend on how the terminal width is obtained. P2-4 and
P2-5 are larger in surface area but carry no risk to correctness, because they
change only presentation.

## Open follow-ups

- **`NO_COLOR=""` still disables color.** `UI::colors_enabled()` returns plain
  output whenever `std::getenv("NO_COLOR")` is non-null, so an *empty* value
  suppresses color too. The no-color.org convention treats an empty `NO_COLOR` as
  unset. This is a semantic question about the environment standard, not the
  U-1 defect, and was deliberately kept out of P1-1 so that unifying the two
  policies would not silently change behavior for anyone exporting the variable
  empty. Needs a deliberate decision, not a drive-by fix.
- **Windows terminal-width detection is unimplemented.**
  `Platform::terminal_width()` returns 0 on Windows, so layout falls back to 80
  columns there. POSIX-first by design and never executed on a Windows machine.
- **Orphaned prompt fragment at narrow widths.** The greedy word wrap used for
  prompts breaks at the last word that fits, so at width 40 the mode prompt
  renders as `Mode [1=Online / 2=Offline] (default` / `    2):`, leaving the
  `2):` alone on a continuation line. Every line still fits the width and the
  continuation indent is consistent, so this is cosmetic and not a blocker. A
  break preference around parenthesized groups would read better, but it must not
  become an ad hoc layout heuristic without tests covering the cases it changes.
  Raised while reviewing P1-2.

### Constraint to respect before the next startup change

The startup block at width 40 measures **25 modeled display lines against a
budget of 25**. There is no slack left. Any future addition to the startup
sequence must be measured at width 40 before it is accepted, rather than
reviewed by inspection.

## Boundaries

- ~~None of the above is implemented, verified, or estimated.~~ **Superseded
  2026-10-12, reconciled against commits.** P1-1 is committed in `f908f2bf`; P1-2
  and P1-3 are committed in `bdf33b2b`. P2-4 and P2-5 remain unimplemented. No
  estimates have been attached to anything.
- No behavioral defect should be masked by a presentation change. Where a
  presentation fix would conceal a behavioral problem, the audit says so
  explicitly, as with truncating long command syntax under P1-3.
- Open lifecycle and platform items remain tracked in `agent-ux-audit.md` and
  are out of scope here.
- Windows presentation behavior is unverified, so every proposal above is
  justified from macOS evidence only and should be re-checked on Windows. This
  now applies to shipped code as well as proposals: the width-aware layout has
  never executed on a Windows machine, where it falls back to 80 columns.