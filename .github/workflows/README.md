# Workflows

What each workflow does and when it fires. Workflow behavior that confuses
people (locked-issue checks, tracking numbers, required secrets) is
documented in `CONTRIBUTING.md` under CI troubleshooting, not here.

| Workflow | Trigger | Purpose |
| --- | --- | --- |
| `agent.yml` | PR, issue, comment, manual | Agent review and issue automation. |
| `ci.yml` | Push, PR, release, manual | Build, coverage, lint, Docker e2e, and `VERSION`-change releases. |
| `cla.yml` | PR opened, sync, reopened | Contributor-agreement check. Needs `CLA_BOT_TOKEN`, `CLA_SIGNED_JSON`. |
| `labeler.yml` | PR | Labels pull requests from commits and files. |
| `lock-merged.yml` | PR | Locks merged pull requests. |
| `macos-trial.yml` | Manual | Isolated measurement of one macOS runner; not part of the build matrix. |
| `maintenance-enqueue.yml` | Manual | Enqueues the prepared report pull request for merge through the merge queue. |
| `maintenance.yml` | Daily 06:00 UTC, manual | Health checks and a dated `docs/maintenance/` report, as a PR. |
| `release.yml` | Tag push, manual | Builds platform binaries and attaches them to the release. |
| `security.yml` | Push, PR, release, weekly, manual | CodeQL, Trivy filesystem scan, and release tarball. |
| `site.yml` | Push touching `docs/`, manual | Deploys `docs/` to GitHub Pages. Shows as `Site` in the runs list. |
| `tracking.yml` | Issue, PR | Maintains the tracking issue from `TRACKING_ISSUE_NUMBER`. |
| `version-bump.yml` | Merged PR | Bumps `VERSION`, tags, and creates the release. Prefers `RELEASE_TOKEN`. |
