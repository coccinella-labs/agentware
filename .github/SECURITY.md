# Security Policy

## Supported Versions

The `main` branch and the latest release. Pre-1.0 means there is no stability
promise, so fixes go to `main` and then into the next release rather than being
backported. The current version is in `VERSION`.

## Scope

In scope is the agentware binary and the source under `src/`: provider calls,
auth, sandboxing, command execution, file and git operations, MCP client
support, and memory persistence.

Out of scope:

- Third-party dependencies. `cpr`, `libcurl`, `nlohmann-json`, `OpenSSL`, and
  `zlib` are maintained upstream; report them to their own security contacts.
  `libpqxx` is optional and disables database support when absent.
  `googletest` is fetched at build time and is not shipped.
- The MCP servers configured in `data/mcp_servers.json`. They are external
  `npx` packages, not part of this repository.
- The generated `build/` directory, which contains no first-party code.
- Documentation under `docs/`.

## Reporting a Vulnerability

Do not create a public issue or pull request. Email
coccinella.labs@icloud.com with:

- Steps to reproduce, including the commit or release you tested
- Potential impact
- A suggested fix, if you have one

We acknowledge within 48 hours and respond within 7 days. Those are targets,
not guarantees.

## Coordination

We ask that you give us time to ship a fix before publishing anything. We
will credit you in the release notes unless you would rather stay anonymous,
and we will not ask you to stop reporting while we work on a fix.

## Disclosure

Fixes ship in a release. Release notes state what was fixed; they do not
include exploit detail. A GitHub Security Advisory is filed when a fix
warrants a CVE, which for a pre-1.0 single-binary tool is not automatic.

Reports are answered by email. There is no mailing list and no notification
system, so we cannot promise to inform anyone beyond the original reporter.
