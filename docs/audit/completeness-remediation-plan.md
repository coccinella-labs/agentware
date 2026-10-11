# Completeness remediation plan

Ordered by risk, then dependency. Each item is an independently
committable change with its regression test. Evidence in
`docs/audit/full-system-audit.md` (§6 IDs). No code changed here; no
pushes; pushed history (`github:threads`, shell routing) is baseline,
not permission to rewrite.

## Phase A — session survival (P1 crashes/hangs)

- **A1 F-02 `replace:` empty-pattern hang.** Guard empty `old_text`
  (validator + service-level, both: caller checks, service returns
  `Error:` on empty). Regression: probe asserting exit 0 + `Error:`
  on `replace:f::new`. Independent.
- **A2 F-01 `components:` crash.** Wrap the `directory_iterator` walk
  (and audit `analyze:` siblings) to return `Error:` on bad paths.
  Regression: probe asserting exit 0 + `Error:` on
  `components:/nonexistent`. Independent of A1.
- **A3 F-05 issues/health throw.** try/catch → `Error:` strings,
  mirroring `threads:`. Regression: fake-endpoint or injected-failure
  probe (needs a seam: extract URL builder or accept endpoint override
  in test builds). Depends on nothing; do with A1/A2.

## Phase B — restore broken capabilities (P1)

- **B1 F-03 `git:log` self-block.** Exempt the service's own `&&`
  construction from the warning gate (validate the path portion only,
  or check result instead of warnings for internal commands).
  Regression: `git:log` probe in repo returns log lines. Independent.
- **B2 F-04 bare `forget/memory/clear`.** Either route colon-less
  bare words for these three to `handle_direct_command`, or remove
  them from help and document the `:` forms. Decision needed; either
  is one commit. Regression: probe asserting the chosen behavior.
  Independent.

## Phase C — UI truth pass (P2)

- **C1 Help rewrite.** Fix fictional entries (`/save`, `/checkpoints`,
  `/github` in `/help`, `Toggle shell`, `@file`), document all live
  subcommands, unify `grep:`/`read:`/`replace:` syntax lines.
  Regression: new help↔parser consistency test enumerating dispatch
  prefixes against help strings (cheap, high-value). Independent.
- **C2 Status displays.** Reuse the 9-entry provider map in `/stats`;
  drop or wire `Memory=Active`/`Commands=Available`; delete
  `print_enterprise_status` (or gate it behind real checks —
  recommend delete); remove `prompt_user`/`print_info`/unused spinner.
  Regression: `/stats` probe asserting provider names. Independent.
- **C3 Small honesties.** `remember:`/empty validation, invalid-regex
  surfacing (`Error: invalid pattern` vs `No matches`), `/error`
  empty-state wording given unwired logging (either wire `log_error`
  at 2-3 key failure points or label the report), HEALTHCHECK +
  `--version` (deferred to release phase per prior plan), double
  Ready line. Each one commit or batched; independent.

## Phase D — dead code and config (P2/P3, careful)

- **D1 Dead config.** Remove `DB_*` from `.env.example` or decide the
  database; clarify SERPAPI line already done. One commit.
- **D2 Dead API triage.** No mass deletion: annotate or remove only
  what misleads (`print_enterprise_status`, shell-mode remnants if
  any). MCP/Auth/Error dead methods stay until their Phase-3
  decisions (per roadmap). Independent.
- **D3 Release/packaging + `prepare-release.sh` grep.** Fix the
  workflow-name grep; leave the tripled release flow for the release
  phase. Independent.

## Dependencies and order

A1, A2, A3, B1, B2 in any order (disjoint code paths). C1 after B2
(help must describe the decided bare-word behavior). C2/C3 anytime.
D anytime. Full suite + probes + preflight after each commit.

## Acceptance

Every P1 has a fail-before/passing-after probe; help claims match the
parser by test, not by reading; no new P1 introduced (re-run the
crash-class probes); Windows/Linux claims stay code-read-only until
their CI runs the tests.
