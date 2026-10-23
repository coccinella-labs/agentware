#!/usr/bin/env bash
# Renders the daily maintenance report.
#
# Usage: render_maintenance_report.sh <report_path> <event_name> <checked_sha>
#
# Reads the five section files (report_build.md, report_tests.md,
# report_version.md, report_deps.md, report_links.md) from the current
# directory, exactly like the workflow does: a section found on disk is
# copied verbatim, a missing section falls back to its "NOT RUN" stub.
#
# Shape contract (pinned by src/tests/maintenance_report_test.sh):
#   - the file ends with exactly one newline, never two;
#   - the blank line goes before each section, never after, so the
#     spacing between sections cannot collapse into a run-on block.
set -u

REPORT="${1:?usage: render_maintenance_report.sh <report_path> <event_name> <checked_sha>}"
EVENT_NAME="${2:?missing event name}"
CHECKED_SHA="${3:?missing checked sha}"

mkdir -p "$(dirname "$REPORT")"
{
  echo "# Maintenance $(date -u +%F) $(date -u +%H%M%S) UTC"
  echo ""
  echo "- run triggered by: $EVENT_NAME"
  echo "- checked commit: $CHECKED_SHA"
  echo ""
  first_section=1
  for section in report_build.md report_tests.md report_version.md report_deps.md report_links.md; do
    # The blank line separates sections, so it goes before each one
    # rather than after. An echo after the loop left a blank line at
    # the end of the file, which end-of-file-fixer in
    # .pre-commit-config.yaml rejects, so every committed report was
    # immediately flagged by the hooks. The steps above write every
    # section with echo, so each one ends in a newline and the report
    # ends with exactly one.
    if [ "$first_section" = 1 ]; then
      first_section=0
    else
      echo ""
    fi
    if [ -f "$section" ]; then
      cat "$section"
    else
      case "$section" in
        report_build.md) echo "## Build" ;;
        report_tests.md) echo "## Tests" ;;
        report_version.md) echo "## Version" ;;
        report_deps.md) echo "## Dependencies" ;;
        report_links.md) echo "## Links" ;;
      esac
      echo "- result: NOT RUN (an earlier check failed first)"
    fi
  done
} > "$REPORT"
