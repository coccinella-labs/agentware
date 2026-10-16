# Agentware: UI Audit (Pass 2)

Presentation-layer audit of what a user actually sees in the terminal. This is
a separate layer from the UX-skeleton audit in `agent-ux-audit.md` and does not
revisit or reopen the behavioral work closed there.

- **Commit under audit:** `851182a5` (branch `main`)
- **Repository state:** unmodified. This audit changed no application code,
  no tests, and no existing audit document.
- **Binary:** freshly configured and built from that commit, 0 compile errors
- **Environment:** macOS. Hermetic `HOME`/`USERPROFILE` redirected to a temp
  directory, `env -i`, offline mode selected, no API key present. All captures
  were written outside the repository.

- **Remediation status (updated 2026-10-12, reconciled against commits):** U-1,
  U-2, and U-5 have since been addressed by P1-1, P1-2, and P1-3 of
  `agent-ui-improvement-proposal.md`, and that work is now **committed**: P1-1 in
  `f908f2bf`, P1-2 and P1-3 in `bdf33b2b`, whose parent is `f908f2bf`. The
  findings below are preserved exactly as audited, including their evidence. Each
  addressed finding carries an inline `Resolution` block recording what changed,
  which commit contains it, and what was measured, so the original observation
  and the later measurement remain distinguishable.
- **`851182a5` is not a UI baseline.** It is the sandbox lifecycle fix bounding
  the `run_bounded` worker, and it supports the POSIX-only Follow-up 2 resolution
  in `agent-ux-audit.md`. The UI baseline is now `bdf33b2b`.
- **Provenance of the measurements below:** the figures in the `Resolution`
  blocks were produced by the implementing agent and reviewed as a report. They
  were not independently re-measured by the reviewer. Every acceptance
  assertion behind them is still reproducible from the working tree.

## Evidence tags

| Tag | Meaning |
| --- | --- |
| **Reproduced** | Captured from the running binary in this audit |
| **Source-confirmed** | Established by source inspection, not re-run here |
| **Unverified** | Not established; explicitly not asserted |

All five findings below are tagged **Reproduced**. **Source-confirmed** is
retained in this legend for future revisions, including the Windows pass this
audit leaves open, and is not used by any finding here.

## Platform boundary

Every capture in this document is macOS. **Windows presentation behavior is
unverified.** No Windows runner executed the UI during this audit, and the
console is not a pty there, so neither color gating, wrapping, nor the
interactive path can be assumed to behave as described.

---

## U-1. Two color policies, and the theme path ignores both TTY and NO_COLOR

**Reproduced.**

`UI::colors_enabled()` in `src/ui.cpp:28-33` checks whether stdout is a TTY and
honors `NO_COLOR`. `ThemeService::is_color_supported()`
(`src/theme_service.cpp:416-426`) consults only `$TERM`. The two policies
therefore disagree, and `/theme` output follows the weaker one.

Measured escape-sequence counts, same binary, same input. Each row was captured
in an isolated run rather than through a shared harness; see the corrections
section below.

| Condition | Startup banner | `/theme current` |
| --- | --- | --- |
| TTY, `TERM=xterm-256color`, no `NO_COLOR` | colored (12 ESC total) | colored |
| TTY, `TERM=xterm-256color`, `NO_COLOR=1` | plain | **colored** |
| Pipe, `TERM=xterm-256color` | plain | **colored** |
| TTY, `TERM=dumb` or unset, no `NO_COLOR` | colored | plain |
| Pipe, `TERM=dumb` or unset, no `NO_COLOR` | plain | plain |

Under `NO_COLOR=1` the entire session contains exactly one escape sequence,
and it comes from the theme path:

```
Current theme: ^[[34mdefault^[[0m
```

Piped output carries the same sequence, so it lands in redirected files, logs,
and CI output as raw control characters.

Note that the shared UI path is correct. This is not a case of color being
unconditionally broken; it is one subsystem bypassing a policy the rest of the
UI already respects.

### Resolution: P1-1. Implemented in `f908f2bf`, measured

`ThemeService::is_color_supported()` now delegates to
`Utils::UI::colors_enabled()` instead of consulting `$TERM`, leaving a single
color decision point. The matrix above is the acceptance criterion: the theme and
shared-UI surfaces must agree in every row.

Measured against that criterion, 8 of 8 conditions pass, including the two rows
that the faulty harness had previously corrupted (`TERM=dumb` and unset `TERM`,
under both TTY and pipe). Supporting coverage is `ColorPolicyTest` (2 cases) in
`src/tests/main_test.cpp` and the `NO_COLOR disables theme escapes` probe.

**U-1 is resolved.** One adjacent gap is explicitly **not** resolved and was
deliberately left out of scope: `NO_COLOR=""` still disables color, because
`colors_enabled()` tests only whether the variable is *present*
(`std::getenv("NO_COLOR") != nullptr`), while the no-color.org convention treats
an empty value as unset. Tracked separately below.

---

## U-2. Startup quick help repeats commands and groups them unclearly

**Reproduced.**

Verbatim startup output:

```
Quick Commands: help | search:query | cmd:command | read:file | quit
version | search:query | cmd:command | read:file | write:file text | exit
```

`search:query`, `cmd:command`, and `read:file` appear on both lines. The second
line opens with `version`, which reads as a continuation of the first rather
than the start of a new group. A first-time user cannot tell which entries are
alternatives and which are the recommended starting set.

### Resolution: P1-2. Implemented in `bdf33b2b`, measured

Quick help is now emitted from a single de-duplicated token list, so a command
cannot repeat across lines by construction. The wrapping is width-aware rather
than a fixed two-line strip.

Measured: zero duplicate tokens at widths 40, 60, 80, and 120. Both spellings for
leaving are listed (`quit` and `exit`) so neither has to be discovered through
full help. At width 40 the block renders as three lines:

```
Quick Commands: help | version
    search:query | cmd:command
    read:file | write:file text | quit
    exit
```

**U-2 is resolved.** Coverage: `LayoutTest.QuickHelpHasNoDuplicateTokens`.

---

## U-3. Help is spread across three screens with overlapping content

**Reproduced.**

| Screen | Content lines | Shape |
| --- | --- | --- |
| `help` | 47 | Seven domain groups: Files, Sessions, Project, Web and GitHub, Checkpoints, Settings and diagnostics, System |
| `/help` | 37 | Flat meta-command list, plus `github:*`, file injection, and shell sections |
| `/tools` | 29 | Direct colon commands grouped by category |

Overlap confirmed in the captures:

- All four `github:*` commands appear in both `help` and `/help`.
- `!<command>` appears in both `help` and `/help`.
- Every colon command appears in both `help` and `/tools`.

A notation conflict for the same command also appears across screens:

```
help    /files <path...> [options]  - Read multiple files
/help   /files <patterns>           - Read multiple files with patterns
```

These are recorded here as an information-hierarchy problem. The deeper
command-coverage discrepancies were already catalogued in `agent-ux-audit.md`
and are not restated.

---

## U-4. Output formatting is inconsistent across commands

**Reproduced.**

Indentation does not follow one convention:

| Output | Indent |
| --- | --- |
| `/stats` title and body | 2 spaces |
| `/sandbox status` title and body | 0 |
| `/error report` title and body | 0 |
| `/theme current` title | 2 |
| `/theme current` description line | 0 |

`/theme current` is inconsistent with itself across two adjacent lines:

```
  Current theme: default
Description: Default light theme
```

The shared `[OK]`, `[WARN]`, `[ERROR]`, and `[INFO]` helpers exist in
`src/ui.cpp` but are not used by any of these command outputs, so there is no
consistent status convention across the command surface.

Error guidance is also uneven between the two dispatch families:

```
> Unknown meta command: /nosuchcmd
 Type /help for available commands.     <- echoes input, gives a next step

> Unknown command                        <- bare: no echo, no next step
```

Both were produced in the same session. A user who mistypes one form gets
guidance; the other form gives them nothing to act on.

---

## U-5. Narrow terminals break the manual alignment

**Reproduced by width measurement, with a stated limitation.**

A pty does not insert line breaks for wrapping; that is a display-side effect,
so no raw byte capture can show how output looks when it soft-wraps. The
findings below are therefore measured printed widths, from which wrapping is
inferred rather than observed.

| Screen | Longest line | Lines over 40 columns |
| --- | --- | --- |
| Startup | **136** | 5 of 22 |
| `help` | 136 | **39 of 69** |
| `/help` | 136 | 32 of 54 |
| `/tools` | 136 | 26 of 46 |

The 136-column startup line is the mode and model prompts printed on one line,
because the prompts carry no trailing newline:

```
Mode [1=Online / 2=Offline] (default 2): Model [1=llama3.2:3b / 2=llama3.2:latest / 3=llama3.1:latest] (default 1): [OK] llama3.2:latest
```

At 40 columns that single line wraps to roughly four lines of running text
before the user reaches a status line.

The status line itself is 81 columns:

```
System: Mode=Offline | Model=llama3.2:latest | Memory=Active | Commands=Available
```

Help alignment is produced by literal space padding with no wrapping logic, so
once the terminal wraps, the description column separates from the command it
describes. Affected entries include:

```
   63 |   analyze: | components: | todos: | tree: - Inspect the project
   66 |   github:threads:<owner>/<repo>:<pr>[:query] - List review threads
   75 |   /checkpoint <command>      - Create, list, inspect, or delete checkpoints
```

### Resolution: P1-3. Implemented in `bdf33b2b`, measured

Literal space padding is replaced by width-aware layout. Terminal width comes
from `ioctl(STDOUT_FILENO, TIOCGWINSZ)` with a validated result and an
80-column fallback when detection fails. `COLUMNS` is deliberately **not**
consulted, because it is routinely stale in non-interactive shells. `help`, the
status line, quick help, and the setup prompts all derive layout from that
width through pure builders that take an explicit width, so they can be asserted
without a live terminal.

Re-measuring the table above at 40, 60, 80, and 120 columns:

| Measure | Before | After, at 40 columns |
| --- | --- | --- |
| Longest startup line | 136 | **40** |
| Startup lines over 40 columns | 5 of 22 | **0** |
| Status line | 81 columns | 40, wrapping with a 4-space continuation indent |
| `help` lines over 40 columns | 39 of 69 | **1 of 90** |
| Modeled startup display height | 25 | 25 |

The single remaining over-width `help` line is
`github:threads:<owner>/<repo>:<pr>[:query]` at 44 columns including its indent.
It is one unsplittable token, which the P1-3 proposal named in advance as the
sole case permitted to exceed the width rather than be truncated. Command syntax
is preserved verbatim; no entry is truncated at any width.

The 136-column mode/model line is gone: the prompts now occupy separate logical
lines, and each is wrapped to the detected width. Piped output, where detection
fails, uses the documented 80-column fallback, and that path is asserted
separately rather than assumed.

**U-5 is resolved**, with the one documented unsplittable-token exception.

Two things this did **not** fix, tracked separately below: the orphaned `2):`
prompt fragment that remains at width 40, and Windows width detection, which is
unimplemented and falls back to 80 there.

Coverage: 12 `LayoutTest` cases in `src/tests/main_test.cpp` assert the layout
builders directly at widths 24, 40, 60, 80, and 120; an 11-of-11 acceptance run
exercises real ptys at 40/60/80/120 plus a piped fallback run.

---

## Corrections made during this audit

Three intermediate readings were wrong and are recorded so they are not repeated.

**The logo is colored.** An early capture appeared to show the ASCII logo
uncolored. That was a capture truncation artifact on my side. The logo is
correctly rendered in pink, `ESC[38;2;243;75;125m`, which is `#F34B7D`, matching
the documented brand color. The omission was never present in the product.

**The first `NO_COLOR` capture was invalid.** It reported zero escape bytes,
which would have contradicted U-1. The cause was my capture method: the input
lines were not consumed, so no commands ran and there was no theme output to
measure. Re-running with paced input reproduced the finding exactly as reported.
A zero-escape result from a session in which the commands did not execute is
not evidence, and should not have been treated as a result.

**Two rows of the U-1 matrix were produced by a faulty harness.** An earlier
version of this matrix reported the startup banner as plain under a real TTY,
including for `TERM=dumb` and for `TERM` unset. That contradicted the source,
which is correct on this point: `colors_enabled()` tests TTY status and
`NO_COLOR` and never inspects `TERM`, so a real TTY colors regardless of
`TERM`. The rows were measured by a shared shell helper that was
mis-reporting, and they were published as reproduced without being re-measured
in isolation.

Both rows were re-captured one condition per run. Under a real TTY with
`TERM=dumb` the banner emits 11 escape bytes, and with `TERM` unset it is also
colored. The corrected rows appear above and the pipe case is unchanged. The
split into separate TTY and pipe rows matters: it was conflating them that let
the error through.

The underlying finding is unaffected. U-1 stands: the theme path ignores both
TTY detection and `NO_COLOR`. What was wrong was the evidence matrix, not the
observation.

---

## What is working well

Recording this so a proposal does not regress it.

- **The shared color policy is correct.** TTY gating and `NO_COLOR` are both
  honored by the main UI, verified across pipe and TTY.
- **Spinner and carriage returns are suppressed when piped.** A TTY capture
  contained 23 carriage returns from the spinner; a piped capture contained 0.
- **Docker status messaging distinguishes its two failure modes accurately:**
  ```
  Docker CLI not found on PATH. Install Docker to use sandbox features.
  Docker CLI found (Docker version 29.8.1, build ...) but the daemon is not
  responding. Start Docker to use sandbox features.
  ```
- **The host fallback states its consequence and denies by default:**
  ```
  Sandbox unavailable (Docker daemon not reachable): this command would run
  on the host without isolation.
  Run on host without isolation? (y/N):   ->  Command cancelled by user
  ```
- **The overwrite confirmation names the file, states the consequence, and
  preserves content on refusal.** Declining left the original bytes intact.

---

## Limitations

These are recorded separately and are **not** presentation findings. They come
from the UX-skeleton work and remain open:

- **Windows process-tree leak.** Unresolved and unverified at runtime. No
  Windows runner executed these paths.
- **`docker stop` worker timeout.** Separate fix, no dedicated test yet.
- **Blocking `waitpid` after a read error.** Pre-existing unbounded wait.
- **Descendants escaping the process group via `setsid`** may survive.
- **Ctrl-C residual.** A local Docker client can outlive the agent. `851182a5`
  makes this explicit rather than fixing it: because the child no longer shares
  the terminal's foreground process group, a terminal Ctrl-C no longer reaches
  it, and the agent installs no signal handler.
- **Windows presentation generally.** No Windows capture exists for any claim
  in this document.

### Open items introduced or deferred by the P1 work

- **`NO_COLOR=""` disables color.** `colors_enabled()` treats any *present*
  value as a request for plain output, so an empty value also suppresses color.
  The no-color.org convention treats an empty value as unset. Deliberately not
  changed under P1-1, which was scoped to unifying the two policies.
- **Windows terminal-width detection is unimplemented.** `terminal_width()`
  returns 0 on Windows, so layout falls back to 80 there. POSIX-first by design;
  not verified on any Windows machine.
- **Orphaned `2):` prompt fragment.** Cosmetic only; see the follow-up in
  `agent-ui-improvement-proposal.md`.

## Unverified items

- How any of the above renders on Windows.
- Rendering in terminals narrower than 40 columns, and wider than the measured
  maxima. Partially advanced: the layout builders are asserted at widths 24, 40,
  60, 80, and 120, but live captures were taken at 40, 60, 80, and 120 only, and
  only on macOS.
- Rendering in terminals without truecolor support, where `#F34B7D` must be
  downgraded.
- Screen-reader behavior, which is out of scope for this audit.