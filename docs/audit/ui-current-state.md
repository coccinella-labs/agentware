# UI current state

Operational summary of the Agentware terminal UI. This document answers: what
is true at `HEAD`, what remains open, and where the evidence lives. It is not an
audit report.

> **Governing rule.** The word "current" is never used here without naming which
> state it means. Any claim below is scoped to a named revision. Source in this
> repository now matches `HEAD`: there is no uncommitted application code, so
> the historical hazard of reading the working tree as if it were `main` no
> longer applies. The P1 presentation work is committed and reviewable.

| Document | Responsibility |
| --- | --- |
| `ui-current-state.md` (this file) | Status summary and where to look for evidence |
| `agent-ui-audit.md` | Findings, original evidence, resolutions, measurement provenance |
| `agent-ui-improvement-proposal.md` | Proposed work and implementation status |
| `agent-ux-audit.md` | Behavior and lifecycle findings (presentation out of scope) |

Figures are duplicated here only where needed to orient a reader. Detailed
measurements and acceptance assertions have one authoritative home in
`agent-ui-audit.md`; if a number here ever disagrees with that document, that
document is correct.

### Revision roles

`851182a5` and `bdf33b2b` do different jobs and must not be conflated:

| Revision | Role |
| --- | --- |
| `851182a5` | Sandbox lifecycle fix bounding the `run_bounded` worker. **Supports the POSIX-only Follow-up 2 resolution in `agent-ux-audit.md`.** Not a UI baseline. |
| `f908f2bf` | P1-1, color-policy unification. |
| `bdf33b2b` | P1-2 and P1-3, terminal width and width-aware UI. **Current `HEAD` and UI baseline.** |

## 1. Committed baseline: `HEAD` at `bdf33b2b`

What a fresh clone of `main` contains once these commits are published. Until
the two commits are pushed, a fresh clone of `origin/main` has only `851182a5`
and therefore none of this.

### P1-1: color-policy unification (`f908f2bf`)

`ThemeService::is_color_supported()` defers to `Utils::UI::colors_enabled()`,
which tests TTY state and `NO_COLOR` and never inspects `TERM`, so a real
terminal colors even when `TERM=dumb`. Only the *gating* was unified: no palette
or theme behavior changed.

### P1-2 and P1-3: width-aware layout (`bdf33b2b`)

Terminal width comes from `ioctl(STDOUT_FILENO, TIOCGWINSZ)` with a validated
result, falling back to 80 columns when detection fails. `COLUMNS` is
deliberately **not** consulted: that is an intentional exclusion, not a missing
feature, because it is routinely stale in non-interactive shells. Help, the
status line, quick help, and the setup prompts derive layout from that width
through pure builders that take an explicit width, so they are assertable
without a live terminal.

Quick help is generated from one de-duplicated list, so a command cannot repeat
across lines. Both spellings for leaving (`quit`, `exit`) are listed.

Long command syntax is never truncated. `github:threads:<owner>/<repo>:<pr>
[:query]` is a single unsplittable token and is the one entry permitted to
exceed the width.

At width 40 the longest startup line fell from 136 to 40, with none over the
width, and `help` lines over 40 columns fell from 39 of 69 to 1 of 90.

### Known regression risk at commit time

`src/agent.cpp` belongs to `bdf33b2b` (P1-2/P1-3) even though it is not a UI
file: `Agent::get_user_choice` calls the P1-3 prompt wrapper. Reconstructing
`f908f2bf` without it fails to compile. Any future split of these commits must
carry that dependency.

## 2. Startup layout budget: read before changing startup

The startup block measures **25 modeled display lines at width 40, against a
budget of 25. There is no slack.** Any change to the startup sequence must be
measured at width 40 before acceptance, not reviewed by inspection. Soft-wrapped
counts are modeled from printed widths because a pty does not record
display-side wrapping.

Detailed measurements, the per-width table, and the acceptance assertions live in
`agent-ui-audit.md` under U-5.

**Provenance:** every figure in this document was measured by the implementing
agent from the working tree, on macOS, and reviewed as a report. The reviewer
did not independently re-run the build or the tests.

## 3. Verified limitations and open work

Presentation items:

- **Windows terminal-width detection is unimplemented.** `terminal_width()`
  returns 0 there, so layout falls back to 80 columns. POSIX-first by design and
  **never executed on a Windows machine**.
- **All Windows presentation behavior is unverified.** No Windows runner
  executed any UI path.
- **`NO_COLOR=""` disables color.** `colors_enabled()` tests only whether the
  variable is *present*; the no-color.org convention treats an empty value as
  unset. Deliberately kept out of P1-1 so unifying the two policies would not
  silently change behavior.
- **Orphaned `2):` prompt fragment.** At width 40 the mode prompt breaks as
  `Mode [1=Online / 2=Offline] (default` / `    2):`. Cosmetic only; every line
  still fits and the continuation indent is consistent. Do not fix it with an
  untested layout heuristic.
- **Theme selection changes nothing** outside `/theme`'s own output. P1-1
  unified *gating*, not palettes; this is not addressed by that work.
- **Startup status is unprobed.** `Memory=Active` and `Commands=Available` remain
  hardcoded.
- **Spinner frames leak into piped output** (`\r|` carriage returns).
- **Help screens are unreconciled** across `help`, `/help`, and `/tools`, with
  known coverage and notation drift.

Proposed work not started: **P2-4** (reconcile help screens) and **P2-5**
(consistent indentation, status helpers, error guidance), tracked in
`agent-ui-improvement-proposal.md`. Neither is closed merely because P1 landed.

Lifecycle items are out of presentation scope and tracked in `agent-ux-audit.md`.
Of its eight follow-ups, **Follow-up 2 is resolved on POSIX only** by
`851182a5`; follow-ups 1 and 3 through 8 remain open, as do the Windows
process-tree leak, `setsid` escape, `docker stop` timeout, and the read-error
`waitpid` path explicitly excluded from that resolution.

## 4. Historical findings

Superseded observations are retained for traceability rather than reproduced
here. Each is marked, with the state that supersedes it, in the documents below.

- `ui-captures/README.md`: all captures are **pre-fix baseline evidence**
  recorded against `851182a5`, before the P1 work; per-file supersession table.
- `agent-ui-audit.md`: U-1, U-2, U-5 resolved by `f908f2bf` and `bdf33b2b`;
  U-3, U-4 open. Preserves original evidence and the corrections made during
  that audit.
- `ui-current-state.md` Appendix A: the original 2026-10-11 live-execution
  record, covering 13 UI states, captured at `851182a5`. Still the only record of
  what was executed and what was not tested.
- `ui-rendering-inventory.md`, `ui-modernization-proposal.md`,
  `ui-ux-modernization-audit.md`, `remediation-plan-product-ui.md`: earlier
  proposal and source-map records, each carrying a dated status banner.

### When the baseline changes again

The rule this document previously carried, when P1 was uncommitted: for each
item, verify which changes a commit actually contains before reclassifying it,
move only confirmed behavior into the baseline, and preserve measurement
provenance. That rule was applied to `f908f2bf` and `bdf33b2b`. It still applies
to any future baseline change, and `851182a5` must retain its identity as the
sandbox lifecycle fix regardless of which UI revision is current.

---

## Appendix A. Capture record of 2026-10-11 (historical, superseded in part)

**Original record: "UI current state (evidence capture)".** Reproduced
verbatim below, demoted one heading level so it cannot be confused with
the sections above. Its claims describe `HEAD` and pre-P1 behavior only.

Audit-only record of Agentware's actual UI as built and run on
2026-10-11. No code, style, behavior, config, or test changed.
Raw terminal captures alongside this file: `ui-captures/`.

- Build/run: `cmake --build build --target agentware-agent` then
  `./build/bin/agentware-agent` (binary `build/bin/agentware-agent`,
  1500256 bytes). Environment: macOS 27.0.1, piped stdin/stdout
  (non-tty) except one `script(1)` pty run; `HOME` pointed at a temp
  dir per session; only fake credentials used (`FAKEKEY123`,
  `key=fake`); temp working dirs only.
- Screenshots: not available — no display-capture tool for a live
  terminal session in this environment. `ui-captures/startup-raw.txt`
  preserves the exact byte stream including ANSI escapes instead.

### Status note (2026-10-12)

This record is preserved as captured on 2026-10-11 at `851182a5` and is **not**
current. Three of its observations are superseded by committed work
(`f908f2bf`, `bdf33b2b`); see section 1 above, `agent-ui-audit.md`, and
`agent-ui-improvement-proposal.md`.

- Section 1's "`NO_COLOR=1`/`TERM=dumb` ignored (10 escape-bearing lines under
  both)" is **no longer true.** The theme path now follows the shared UI policy
  (P1-1, `f908f2bf`), so both surfaces agree in all measured conditions.
- Section 13's "No wrapping, truncation, or color-disable support" is **no longer
  true.** Output is width-aware (P1-3, `bdf33b2b`) and color-disable support
  exists (P1-1, `f908f2bf`).
- `COLUMNS=40` being ignored is **still true, by decision**: width now comes from
  `ioctl(TIOCGWINSZ)` and `COLUMNS` is deliberately not consulted.
- Section 4's finding that `/theme` alters nothing outside its own output is
  **still true.** P1-1 unified gating only; no UI output reads the palette.

The captures this document cites are pre-fix baseline evidence, recorded before
the P1 commits. Nothing below reflects the current tree at `bdf33b2b`.

### 1. Startup sequence (verbatim, `ui-captures/startup.txt`)

Pink 8-line block logo (`Color::PINK`, truecolor `38;2;243;75;125`),
`Agentware`, `Welcome to Coccinella Labs`, 40-dash divider, then
`Mode [1=Online / 2=Offline] (default 2):` and model/provider select,
`[OK] llama3.2:3b`, the `System:` line, two quick-help lines, divider,
single `Ready - Type a command or chat naturally:`, `> ` prompt.
`exit` prints `Goodbye` + `Agent run completed`, exit 0.

Observed issues: none blocking. Colors are emitted even when piped
(raw bytes confirm `^[[38;2;...`); `NO_COLOR=1`/`TERM=dumb` ignored
(10 escape-bearing lines under both). `COLUMNS=40` ignored (lines up
to 145 cols). The duplicate `Ready - Type a command:` line reported
in earlier audits is gone — one Ready line remains.

### 2. State-by-state results

Executed live unless marked static-only. `not tested` where noted.

1. **Startup/ready prompt** — executed. Above. No issue found.
2. **Help + unknown commands** — executed (`help`, `/tools`, `frobnicate:xyz`
   → `Unknown command`, `/frobnicate` → `Unknown meta command` +
   `Type /help for available commands.`). `help` now documents
   `github:threads:`, `analyze:`, bare `memory | forget | clear`.
   Issue: `/tools` omits `!`, `help`, `quit`, `write:`, `/files`
   (P3); arg-name drift `grep [:dir[:ext]]` (`/tools`) vs
   `[:dir[:filter]]` (`help`) (P3).
3. **`/stats`, provider status** — executed, all 6 online providers
   with fake keys + offline models. `Mode:` prints the correct
   provider/model in every case (the old collapse bug is fixed).
   `Commands Processed` counts. `Memory=Active` / `Commands=Available`
   still hardcoded. `version` prints `Agentware v0.1.6` + build date.
4. **`/theme`** — executed. `list`/`set dark`/`current`/`preview`/
   bad-name error all behave. Proven by diff: changing theme alters
   **nothing** outside `/theme`'s own output — help, logo, and prompts
   render byte-identically (`ThemeService` colorizes only its own
   handlers; `ui.cpp` uses fixed `Color::` constants). P2 honesty gap.
5. **`/auth`** — executed with fake keys. `providers`/`key`/`status`
   (`Key present (not verified)`)/`test` (`not verified - no live
   check`) all accurate. No secrets involved.
6. **Files/codebase** — executed: read/range/malformed-range,
   write+read-back, replace, grep, todos, analyze, components, tree,
   `/files`, invalid paths, traversal refusals. All match the
   remediation record. No issue found.
7. **Git/GitHub** — executed where hermetic: `git:status` works,
   `git:log` works, bad specs → usage strings, `github:threads:o/r:abc`
   → usage. Live `github:repo` and review-thread listing **not tested**
   (needs network + credentials; covered by unit tests per prior work).
8. **Shell execution** — executed. `!` bare → `Usage: !<command>`;
   `!echo hi` with no daemon → `Sandboxed (docker, default config, no
   network)` then the real daemon error + `Exit code: 1`, session
   survives. Opt-out (`N`) cancels; opt-in (`y`) runs on host.
   No destructive command run. Timeout path **not tested** live
   (needs a hanging command + daemon or long waits; covered by unit
   tests per prior work).
9. **Memory** — executed full cycle: `remember` → `memory` shows fact
   → `forget` → `No global memories stored` → `clear`. No issue found.
10. **Checkpoints/conversation** — executed: `create demo` →
    ID `e8bd7ea1`; `list` shows it; `/chat save/list/resume` work.
    **P2 bug**: `info`/`restore`/`delete` take the **ID**, but nothing
    says so — `restore demo` / `delete demo` fail with `Checkpoint
    not found: demo`. Works only if the user guesses IDs.
11. **Spinner/progress** — partially executed. The `\r|` threaded
    spinner frames leak into piped output (`> |  Error: AI service…`
    in captures). Interruption (SIGINT) **not tested**. P3.
12. **Failure states** — executed: malformed ranges, bad URLs,
    unreachable fetch (`Error fetching URL`), missing files,
    traversal refusals, unknown commands, empty results
    (`No checkpoints found.`, `No recent errors.` + empty report
    sections). All controlled, session survives. Network-failure
    variants beyond loopback-unreachable **not tested**.
13. **Widths/wrapping/pipes** — executed as above. No wrapping,
    truncation, or color-disable support. Piped output is identical
    to terminal output (pty diff shows echo artifacts only).

### 3. Five most important UI improvements (priority order)

1. Make checkpoint ID-vs-name explicit (show usage hint + accept
   unambiguous name prefixes).
2. Make theme selection actually restyle the UI, or label `/theme`
   as preview-only configuration.
3. Add `NO_COLOR` + tty-aware color handling (standards-respecting,
   tiny, testable).
4. Reconcile `help` vs `/tools` coverage and the `ext`/`filter`
   naming drift.
5. Suppress or redirect the spinner when stdout is not a tty.

### 4. Limitations

macOS-only runs; no screenshots; no live network/API providers;
no Docker daemon; no SIGINT test; no screen-reader/contrast audit.
Prior audit docs describing older UI text (double Ready line,
`Local Ollama` collapse) are stale on exactly those points —
both are fixed in this tree.
