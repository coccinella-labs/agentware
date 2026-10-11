# Contributing

This project is currently maintained by collaborators. External contributions are not accepted at this time.

## For Collaborators

1. Fork the repository and create a feature branch
2. Follow Conventional Commits: `feat:`, `fix:`, `docs:`, `chore:`
3. Run `make preflight` before pushing
4. Create a PR for review

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --build build --target test
```

## CI troubleshooting

Notes from maintaining the workflows. The standalone `.opencode-agent.md`
previously held this; it now lives here so there is one place to look.

### CMake config warnings

CMake feature detection tests fail on Linux for Windows-specific types. These
are warnings, not errors. The configure step pipes through `tee` with
`|| true` so warnings do not fail the workflow. Only actual compilation
errors, matching `error:.*\.(cpp|c|h|cc|cxx):[0-9]+:`, count as build
failures.

### Locked issues and PRs

Workflows that comment must check the locked flag first, since several handle
both issues and PRs:

```javascript
const item = context.payload.issue || context.payload.pull_request;
if (!item?.locked) {
  await github.rest.issues.createComment({...});
}
```

### Tracking issue number

Use the repository variable, never a hardcoded number:

```yaml
const trackingIssueNumber = '${{ vars.TRACKING_ISSUE_NUMBER }}';
if (!trackingIssueNumber) return;
```

The same applies to workflow conditions: `vars.` for repository variables,
not `env.`:

```yaml
if: vars.TRACKING_ISSUE_NUMBER != ''
```

### Package names

Use `libattr1-dev`, not `libxattr-dev`, on Ubuntu.
