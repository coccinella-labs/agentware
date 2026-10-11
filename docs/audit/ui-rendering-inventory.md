# UI rendering inventory (source map)

Code inspection of the current tree (no behavior changes). "Verified
live" means observed in the captures alongside
`docs/audit/ui-current-state.md`; otherwise code-inspected only.

## Entry and REPL flow

- `src/main.cpp`: `load_environment()` → `print_logo()` →
  `Core::Agent::run()` → `Agent run completed`; `catch` →
  `Fatal error:` exit 1.
- `Agent::run` → `print_ready_interface(mode, model)` →
  `print_system_info` + `print_quick_help` + single Ready line →
  `> ` loop (`src/agent.cpp`, `src/ui.cpp:255-269`).

## Renderer functions (`src/ui.cpp`, 281 lines)

| Function | Lines | Role | Live check |
|---|---|---|---|
| `print_logo` | 31–55 | 8-line `string_view` art, `PINK`, name, welcome, divider | byte-exact in captures |
| `print_help` | 57–~145 | full command reference | captured; content improved vs older audits |
| `print_enterprise_status` | 147–~178 | hardcoded `4/4` capability grid | **zero callers** outside `ui.cpp` (grep) — dead |
| `spinner(message, ms)` | 182–196 | blocking `\b`-frame spinner | **zero callers** — dead; only atomic version used (2 sites) |
| `spinner(atomic&)` | 199–209 | `\r`-frame thread spinner | frames leak into piped output (captured) |
| `print_success/error/warning` | 212–222 | `[OK]/[ERROR]/[WARN]` prefixes | live in captures |
| `print_info` | 224–226 | `[INFO]` prefix | **zero callers** — dead |
| `print_divider` | 229–232 | 40-dash DIM rule | live, 5 call sites |
| `print_quick_help` | 235–252 | two duplicated command lines | live; lines overlap (`quit` vs `exit` split) |
| `print_system_info` | 255–261 | `Mode/Model/Memory/Commands` | live; latter two hardcoded |
| `print_ready_interface` | 264–269 | composes the above | live; duplicate Ready line gone |
| `prompt_user` | 272–280 | `[You] … >` + getline | **zero callers** — dead |

## Color, width, unicode, disable behavior

- Constants only (`Color::RESET/GREEN/YELLOW/RED/CYAN/BOLD/DIM`,
  truecolor `PINK`), emitted **unconditionally** (`src/ui.cpp:17-28`).
- No `isatty` check, no `NO_COLOR`/`TERM` handling, no `COLUMNS`/`ioctl`
  width logic, no `setw`/wrapping anywhere in `src/ui.cpp` (grep).
  The `<unistd.h>` include is leftover (no longer used).
- Verified live: escapes present when piped, under `TERM=dumb` +
  `NO_COLOR=1`, and past width 40 with `COLUMNS=40`.
- Unicode: logo `█` blocks only; spinner `|/-\`; box-drawing only in
  checkpoint tree output. No width-sensitive layout depends on them.

## ThemeService integration

- `ThemeService` (`src/theme_service.cpp`) owns Dark/Ocean/Cyberpunk
  palettes and `colorize_*` helpers; `/theme` handlers call them
  (`src/agent.cpp:1986-2026`). `dark` is a valid theme (verified).
- **Nothing else reads the theme.** All 99 `Color::` uses in `src/ui.cpp`
  are fixed constants; prompts, help, logo, errors ignore the active
  theme (proven by byte-identical help output under two themes).
- So "theme" today = `/theme`-scoped preview strings, not UI theming.

## Provider/model display

- Ready line + `/stats` print the selected provider/model from live
  session state; all 6 online providers verified with fake keys, plus
  offline models. `version` prints `Agentware v0.1.6` + build date.
- `/stats` no longer collapses providers (old audit stale on this).

## Interactive vs piped output

- Identical rendering: pty-vs-pipe diff shows echo artifacts only.
  Consequence: spinners assume a tty (`\r`/`\b` garbage in logs) and
  colors cannot be disabled — the two P3 portability findings.

## Dependencies and constraints

- None beyond the standard library (`<iostream>`, `<thread>`,
  `<chrono>`), ANSI-capable output, and UTF-8 for `█`. No UI
  framework, no pager, no readline. No new framework is warranted:
  the gaps are truthfulness/consistency issues, fixable in place.
