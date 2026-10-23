#!/usr/bin/env bash
# Regression test for the daily maintenance report generator.
#
# The generator's fix (PR #180) moved the section separator to the front
# of each section, so the report ends with exactly one newline and every
# section keeps the blank line ahead of it. Nothing tested it, and this
# defect has two failure modes that an end-of-file check cannot tell
# apart:
#
#   1. the old bug: a trailing blank line (0a0a at EOF);
#   2. the obvious wrong fix: deleting the separator echo entirely, which
#      also ends the file correctly and destroys every section boundary
#      at the same time.
#
# The test therefore runs the exact generator the workflow calls
# (.github/scripts/render_maintenance_report.sh), not a copy of it, with
# a mix of present and missing sections so both heading paths are
# exercised: sections copied verbatim from files, and the "NOT RUN"
# stubs for the missing ones. Every assertion looks at the full output,
# not just the last two bytes.
#
# POSIX-only.
#
# Usage: maintenance_report_test.sh
# Exits 0 when every case passes, 1 otherwise.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
GENERATOR="$ROOT/../../.github/scripts/render_maintenance_report.sh"
[ -f "$GENERATOR" ] || { echo "maintenance_report_test.sh: generator not found: $GENERATOR"; exit 1; }

fails=0
ok() { echo "  ok: $1"; }
bad() { printf '  FAIL: %s\n' "$1"; fails=$((fails + 1)); }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# Section files the way the workflow writes them: each starts with its
# own heading and ends in exactly one newline. Dependencies and Links
# stay missing, which exercises the generator's fallback stubs.
printf '## Build\n- result: PASS\n' > "$work/report_build.md"
printf '## Tests\n- result: PASS\n' > "$work/report_tests.md"
printf '## Version\n- result: PASS\n' > "$work/report_version.md"

cd "$work"
bash "$GENERATOR" report.md schedule aabbccdd
echo "--- rendered report ---"
cat report.md
echo "-----------------------"

# 1. The file ends with exactly one newline.
last_two_bytes=$(tail -c 2 report.md | od -An -tx1 | tr -d ' \n')
if [ "$last_two_bytes" = "0a0a" ]; then
  bad "the report ends with a blank line (last two bytes: $last_two_bytes)"
else
  ok "the report does not end with a blank line"
fi
last_byte=$(tail -c 1 report.md | od -An -tx1 | tr -d ' \n')
if [ "$last_byte" = "0a" ]; then
  ok "the report ends with a newline"
else
  bad "the report does not end with a newline (last byte: $last_byte)"
fi

# 2. Full-output structural check: every '## ' heading is preceded by a
# blank line, in both heading paths (copied sections and NOT RUN stubs).
runon=$(awk '$0 ~ /^## / && prev != "" {print NR} {prev = $0}' report.md)
if [ -z "$runon" ]; then
  ok "every '## ' heading is preceded by a blank line"
else
  bad "'## ' heading(s) without a preceding blank line at line(s): $(echo $runon)"
fi

# 3. One heading per section, in the workflow's order.
order=$(grep '^## ' report.md | awk '{printf "%s,", $2}')
if [ "$order" = "Build,Tests,Version,Dependencies,Links," ]; then
  ok "section order intact: Build, Tests, Version, Dependencies, Links"
else
  bad "unexpected section order: $order"
fi

# 4. NOT RUN only for the sections that were missing.
notrun=$(grep -c 'result: NOT RUN (an earlier check failed first)' report.md)
if [ "$notrun" = "2" ]; then
  ok "the two missing sections are the only NOT RUN entries"
else
  bad "expected exactly 2 NOT RUN entries, found $notrun"
fi

if [ "$fails" -gt 0 ]; then
  echo "maintenance_report_test.sh: $fails failure(s)"
  exit 1
fi
echo "maintenance_report_test.sh: all passed"
exit 0
