# Maintenance reports

One file per daily run, named `YYYY-MM-DD.md`. Each records which commit was
checked and the result of every check: build, tests, binary version,
dependencies, and documentation links.

A red run means a check failed; the log says which one. Fixing whatever it
found happens through the normal pull request process. Nothing in this
directory is documentation of the project itself. For how the reports are
produced, see `.github/workflows/maintenance.yml`.
