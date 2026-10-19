# UI modernization proposal

Prioritized, proportionate changes only — no redesign, no framework.
Each item states current vs proposed behavior plus regression cover.
Severity follows the repo's P0–P3 scale. No P0 found; nothing here
blocks safe use.

## Status note (2026-10-12)

Items are preserved as originally proposed. Status against the current tree
(`bdf33b2b`):

- **"ANSI always emitted, even piped or under `NO_COLOR=1` / `TERM=dumb`"**
  **implemented (P1-1, `f908f2bf`).** Both surfaces now honor TTY state and `NO_COLOR`.
- **"duplicate quick-help lines (collapse to one line)"**: **implemented
  (P1-2).** Quick help is generated from one de-duplicated list.
- **"No responsive layout system" / narrow-width items**: **implemented (P1-3, `bdf33b2b`)**
  for width-aware help, status, quick help, and prompts.
- **P2-2, theme selection changes nothing globally: still open.** P1-1 unified
  gating, not palettes. No UI output reads the active theme.

## P2-1. Checkpoint ID-vs-name confusion

- Current: `create` takes a name, `info`/`restore`/`delete` take the
  ID; using the name fails with `Checkpoint not found` (captured).
- Proposed: accept unambiguous name prefixes in `info`/`restore`/
  `delete`, and print `ID - name` plus a one-line usage hint on
  failure. No behavior change for ID users.
- Regression: piped probes for name-prefix restore/delete and the
  existing ID path.

## P2-2. Theme selection changes nothing globally

- Current: `/theme set` succeeds and `/theme` output recolors, but
  logo/help/prompts/errors render byte-identically (proven by diff).
- Proposed: either route `Color::` output through the active theme
  palette, or relabel `/theme` as preview-only configuration with a
  note that global theming is not implemented. Former preferred;
  latter acceptable and much smaller.
- Regression: theme-switch diff probe asserting help output differs
  (or the documented label string).

## P3-1. No color-disable / tty awareness

- Current: ANSI always emitted, even piped or under `NO_COLOR=1` /
  `TERM=dumb` (captured).
- Proposed: centralize emission behind `isatty(STDOUT_FILENO)` +
  `NO_COLOR` env check; spinner already has the only tty-dependent
  sequences, so gate it to print a static line when not a tty.
- Regression: existing captures re-run with `NO_COLOR=1` asserting
  zero ESC bytes; spinner probe asserting no `\r` frames when piped.

## P3-2. Help/tools drift (residual)

- Current: `/tools` omits `!`, `help`, `quit`, `write:`, `/files`;
  `ext` vs `filter` naming differs between the two lists.
- Proposed: single source of truth — generate both listings from one
  table, or add the missing entries and unify the arg name.
- Regression: extend the consistency-matrix probes (already in-tree).

## P3-3. Dead UI code

- Current: `print_enterprise_status` (hardcoded `16 features active`),
  `prompt_user`, `print_info`, blocking `spinner` overload, unused
  `<unistd.h>` include — all zero-caller, verified by grep.
- Proposed: delete them (or wire `print_info` somewhere real).
  `print_enterprise_status` must not be wired up as-is — its counts
  are fictional.
- Regression: build + existing suite; add a dead-code grep probe if
  desired.

## P3-4. Minor consistency

- `Memory=Active` / `Commands=Available` hardcoded: drop the fields
  or compute them (memory path writable? commands = matrix of
  backend availability).
- `Goodbye` vs `Goodbye!` exit lines; `quit`/`exit` split across
  duplicate quick-help lines (collapse to one line).
- `grep:pattern[:dir[:ext]]` vs `[:dir[:filter]]` (covered by P3-2).

## Explicitly out of scope

Gradients, animations, icons, marketing copy, pager/readline
integration, screen-reader audit (not performed), live-provider or
live-daemon verification, Windows/Linux runs (CI-only per policy).
