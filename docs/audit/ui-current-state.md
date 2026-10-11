# UI current state (evidence capture)

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

## 1. Startup sequence (verbatim, `ui-captures/startup.txt`)

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

## 2. State-by-state results

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

## 3. Five most important UI improvements (priority order)

1. Make checkpoint ID-vs-name explicit (show usage hint + accept
   unambiguous name prefixes).
2. Make theme selection actually restyle the UI, or label `/theme`
   as preview-only configuration.
3. Add `NO_COLOR` + tty-aware color handling (standards-respecting,
   tiny, testable).
4. Reconcile `help` vs `/tools` coverage and the `ext`/`filter`
   naming drift.
5. Suppress or redirect the spinner when stdout is not a tty.

## 4. Limitations

macOS-only runs; no screenshots; no live network/API providers;
no Docker daemon; no SIGINT test; no screen-reader/contrast audit.
Prior audit docs describing older UI text (double Ready line,
`Local Ollama` collapse) are stale on exactly those points —
both are fixed in this tree.
