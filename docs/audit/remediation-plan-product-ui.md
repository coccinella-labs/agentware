# Product and UI remediation plan

No implementation is authorised by this audit. Each unit below should be a
small reviewable change with its stated regression evidence.

## P0 — truth and safety contracts

1. **Choose MCP: implement transport or remove the operational surface.** Do
   not report list/call/lifecycle results as live until a real stdio lifecycle,
   timeout and tool-call integration test exists.
2. **Create one capability/help registry.** Make `help`, `/help`, `/tools`,
   startup hints and parser tests derive from it. Preserve all live syntax;
   deprecated aliases need explicit migration text.
3. **Normalize execution results.** Introduce a structured internal outcome
   for command/sandbox/web/service paths and render truthful success, failure,
   cancelled, timed-out and partial states. Test Docker-absent and host opt-in
   paths.
4. **Fix test integrity.** Make the local HTTP fixture start failure fatal,
   avoid fixed-port collisions, repair the swallowed `threads` probe, and make
   the suite fail if a declared probe was not run.

## P1 — presentation foundation and state consistency

5. Add terminal profile/plain-output support (`NO_COLOR`, non-TTY, width,
   Unicode fallback), then migrate `UI` and handler output through a few
   reusable primitives. Snapshot 40/80/120-column output.
6. Replace decorative startup claims with verified availability/status, and
   correct `/stats` mode mapping for all `AgentMode` values.
7. Wire ordinary failure boundaries into `ErrorService` or relabel `/error`
   as a manually reported diagnostic store. Assert report contents after a
   real controlled failure.
8. Define persistence locations and ownership. Use an application-data
   directory or explicitly documented project state; add migration and
   checkpoint restore round-trip coverage.

## P2 — integration completeness

9. Add hermetic adapters/fakes for every external boundary: all LLM response
   formats, Ollama, GitHub pagination/errors, HTTP fetch, Docker results and
   file-system failures. Make one success and one meaningful failure journey
   pass through the actual REPL for each boundary.
10. Decide database scope: wire a real capability and configuration validation
    or remove `DatabaseService` and `DB_*` documentation.
11. Consolidate command policy at one choke point and document Docker's
    read/write workspace and network posture. Test validator, host fallback,
    and sandbox behavior against the same acceptance rules.

## P3 — progressive interaction improvements

12. Add contextual usage/suggestions, operation-labelled progress, and
    recovery text. Add cancellation only after defining safe process/network
    cancellation semantics.
13. Verify macOS, Linux and Windows output and ANSI behavior in CI/VMs. Add
    golden semantic-output tests without asserting colour escape sequences.

## Exit criteria

Agentware can be called fully wired only when the feature matrix has no
unreachable advertised feature, every external boundary has hermetic E2E
success/failure coverage, displayed status is derived from measured state,
and the terminal compatibility matrix passes on supported platforms.
