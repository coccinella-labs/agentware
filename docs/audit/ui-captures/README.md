# Pre-fix baseline captures

**Every file in this directory is a pre-fix capture, recorded against
`851182a5`. None of them reflects the current UI, which is `bdf33b2b`.**

These captures were recorded on 2026-10-11 at `HEAD` = `851182a5`. The P1
presentation work that superseded them is now committed: P1-1 in `f908f2bf`, and
P1-2 and P1-3 in `bdf33b2b`. The captures are retained deliberately, unmodified,
as the *before* evidence that `agent-ui-audit.md` measures against. They are not
to be overwritten by re-captures; if a post-fix run is wanted, it belongs in a
separate directory so the comparison stays legible.

Note that `851182a5` is a revision reference here only. Its own role in this
repository is the sandbox lifecycle fix supporting the POSIX-only Follow-up 2
resolution in `agent-ux-audit.md`; it is not a UI commit.

## What has since changed

| Capture file | Superseded by |
| --- | --- |
| `startup.txt`, `startup-raw.txt` | P1-2 and P1-3 (`bdf33b2b`). Startup no longer renders mode and model prompts on one 136-column line, and the startup block is width-aware. |
| `narrow-width.txt` | P1-3 (`bdf33b2b`). The long unwrapped command examples now wrap with a continuation indent. |
| `help.txt` | P1-3 (`bdf33b2b`). Literal space padding is replaced by width-aware layout. |
| `theme.txt` | P1-1 (`f908f2bf`). `/theme` output now honors TTY state and `NO_COLOR` instead of consulting `$TERM`. |

Captures of screens not touched by the P1 work (`stats.txt`, `auth.txt`,
`memory.txt`, `tools.txt`, `failures.txt`, `shell-daemon-down.txt`,
`pty.txt`, `checkpoint-id-vs-name.txt`, `stats-all-providers.txt`) remain valid
as far as this refresh could determine, but they were still recorded at
`851182a5` and were not re-run.

## Caveat

All captures are macOS. **Windows presentation behavior is unverified.** No
Windows runner executed any of these paths, and the console there is not a pty,
so neither color gating nor wrapping can be assumed to behave as shown. The
width-aware layout in particular has never run on Windows, where
`terminal_width()` returns 0 and layout falls back to 80 columns.
