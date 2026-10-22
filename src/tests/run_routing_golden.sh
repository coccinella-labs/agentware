#!/usr/bin/env bash
# Golden routing tests: converts the tool-system audit's 28-case routing
# table into an executable contract.
#
# Each case runs through the REAL dispatch path (classify_input ->
# handle_direct_command / shell / meta) in the built binary, in a hermetic
# temp workspace, and asserts the observable markers: the routed tool call
# line, the tool's output shape, or the failure message. Routing is the
# contract here; content quality is deliberately not asserted.
#
# Model-side cases (malformed JSON, unknown tool name in model output,
# multiple tool calls in one turn) deliberately do NOT appear yet: the
# runtime has no model tool-call parser today. They gain golden cases in
# the PR that adds the model tool-call seam, with canned provider outputs.
#
# Usage: run_routing_golden.sh <path-to-agentware-agent>
# Needs bash, git, python3, and standard POSIX tools; no external network.
set -u

BIN="${1:?usage: run_routing_golden.sh <agentware-agent binary>}"
PORT="${PORT:-0}"
if [ "$PORT" = "0" ]; then
  for p in 18093 18094 18095 18096 18097; do
    if ! (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null; then PORT=$p; break; fi
  done
fi
: "${PORT:=18093}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

# Hermetic home and key state, mirroring remediation_probes.sh.
export HOME="$WORK"
export USERPROFILE="$WORK"
unset SERPAPI_KEY

FAILURES=0
CASES=0

expect() {
  # expect <case-name> <input-line> <extra-stdin-lines> -- <grep-pattern>
  local name="$1" input="$2" extra="$3" pattern="$4"
  shift 4
  CASES=$((CASES + 1))
  local stdin_lines
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  local actual
  actual=$(printf '%b' "$stdin_lines" | "$BIN" 2>&1 |
    perl -pe 's/\e\[[0-9;]*[mK]//g')
  if printf '%s' "$actual" | grep -Fq -- "$pattern"; then
    printf 'PASS  %s\n' "$name"
  else
    FAILURES=$((FAILURES + 1))
    printf 'FAIL  %s\n      input: %s\n      missing: %s\n' "$name" "$input" "$pattern"
    printf '%s\n' "$actual" | sed 's/^/      | /' | head -12
  fi
}

expect_no() {
  # Expects a marker to be ABSENT from the routed output. Currently only
  # meaningful for content guards; ANSI stripping makes absence checks of
  # escape sequences vacuous, so this helper is reserved for negative
  # content assertions (e.g. a cancelled action must not change state).
  local name="$1" input="$2" extra="$3" pattern="$4"
  CASES=$((CASES + 1))
  local stdin_lines
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  local actual
  actual=$(printf '%b' "$stdin_lines" | "$BIN" 2>&1 |
    perl -pe 's/\e\[[0-9;]*[mK]//g')
  if printf '%s' "$actual" | grep -Fq -- "$pattern"; then
    FAILURES=$((FAILURES + 1))
    printf 'FAIL  %s\n      input: %s\n      unexpected: %s\n' "$name" "$input" "$pattern"
  else
    printf 'PASS  %s\n' "$name"
  fi
}

# ----- Fixtures -------------------------------------------------------------
printf 'golden needle alpha\nsecond line\n' > hello.txt
printf 'notes file with needle words\n' > notes.md
git init -q . && git add hello.txt notes.md
git -c user.email=golden@local -c user.name=golden commit -qm "fixture"
if [ "$?" -ne 0 ]; then echo "FATAL: fixture git commit failed"; exit 1; fi

# Make the tree dirty for the git:diff case: an uncommitted tracked change
# whose marker line must appear verbatim in the routed diff output.
printf 'goldendiff uncommitted tracked change\n' >> hello.txt

# Local HTTP fixture for /fetch (deterministic, no external network).
printf '<html><body><h1>golden-fetch-marker</h1></body></html>' > index.html
python3 -m http.server "$PORT" >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null; rm -rf "$WORK"' EXIT
for _ in $(seq 1 20); do
  (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null && break
  sleep 0.2
done

# ----- Routing table (tool + args asserted per case) ------------------------

# NOTE: search: routing itself is covered by the HasPrefixCi unit tests and
# the routing probe; the golden suite deliberately contains no search: case
# because every invocation would attempt an outbound request and the suite's
# contract is zero outbound requests. The capitalized-prefix behavior
# (Search:) is pinned by the case-insensitive unit tests in main_test.cpp.

# 1-3. read routing: plain, ranged, missing, restricted absolute path.
expect "read-plain" "read:hello.txt" "" "golden needle alpha"
expect "read-ranged" "read:hello.txt:1:1" "" 'Tool Read("hello.txt:1:1")'
expect "read-missing" "read:missing.txt" "" "Error: File does not exist"
expect "read-restricted-absolute" "read:/etc/passwd" "" 'Tool Read("/etc/passwd")'

# 4-5. write (new file, no prompt) and replace. The replace success path
# renders the diff instead of a status message, so the effect is asserted
# with a follow-up read in the same session.
rm -f hello2.txt
expect "write-new-file" "write:hello2.txt golden fresh content" "" "written successfully"
expect "replace-applies-diff" "replace:hello2.txt:fresh:verified" "read:hello2.txt" "golden verified content"
expect "replace-missing-file" "replace:no-such-file.txt:a:b" "" "does not exist"

# 6-9. grep routing incl. the audit's wrong-domain defect.
expect "grep-match" "grep:needle:." "" "(via"
expect "grep-filtered" "grep:needle:.:*.md" "" "(via"
expect "grep-no-match" "grep:noneedle-xyz9:." "" "No matches"
expect "grep-empty-pattern" "grep::" "" "grep pattern cannot be empty"
expect "grep-capitalized" "Grep:needle:." "" 'Tool Grep("needle")'
# A pattern the regex engine rejects is an error, not an empty result,
# and the error must be the same whether the host has ripgrep or not.
expect "grep-invalid-pattern" "grep:needle(:" "" "invalid search pattern"
expect "grep-invalid-brace" "grep:needle}:" "" "invalid search pattern"
# These two pass the dialect check (their brackets balance) but no
# regex engine accepts them: an invalid quantifier range and an invalid
# character range. The tool must say so, not report no matches.
expect "grep-invalid-quantifier" "grep:a{2,1}:." "" "invalid search pattern"
expect "grep-invalid-char-range" "grep:[z-a]:." "" "invalid search pattern"
# A brace that is part of a valid quantifier is still valid.
expect "grep-valid-brace-quantifier" "grep:needle{1}:." "" "(via"

# 10-11. git routing, known + unknown subcommand.
expect "git-status" "git:status" "" 'Tool Git("status")'
expect "git-diff" "git:diff" "" "goldendiff"
expect "git-bogus" "git:bogus" "" "Usage: git:"

# 12-14. analysis family.
expect "analyze" "analyze:" "" "Codebase Structure Analysis"
expect "components" "components:" "" "Main Components Analysis"
expect "todos" "todos:." "" "task comments"
expect "tree-fixture" "tree:." "" "hello.txt"

# 15-16. memory bare verbs and fact. Separate invocations, so the fact
# echo is what can be asserted deterministically here.
expect "remember-fact" "remember:golden-fact" "" "Remembered: golden-fact"
expect "remember-echo-lists-fact" "remember:golden-fact2" "" "golden-fact2"
expect "forget-guard" "forget" "" "Global memory cleared"
expect "clear-guard" "clear" "" "Session memory cleared"

# 17. audit regression: prose with a colon must NOT wipe memory.
expect "clear-prose-guard" "clear: the screen please" "" "Unknown command"

# 18. unknown command names what exists (recovery affordance).
expect "unknown-suggestions" "nonsense-verb:" "" "Known commands:"

# 19-21. shell paths: ! direct, cmd: with consent, canceled consent.
expect "bang-shell" "!echo golden-shell-marker" "" "golden-shell-marker"
expect "cmd-confirmed" "cmd:echo golden-cmd-marker" "y" "golden-cmd-marker"
# Docker-independent: "kill" trips validate_command_safe's dangerous-command
# warning, so the "Continue? (y/N): " consent prompt appears before the
# sandbox path is ever reached. Sending "n" cancels deterministically
# regardless of whether Docker is available in the test environment.
expect "cmd-cancelled" "cmd:echo kill golden-cancelled-marker" "n" "Command cancelled by user"

# 22-23. meta commands. Hermetic HOME starts with no saves; assert the
# list behavior, not its population.
expect "chat-list" "/chat list" "" "saved conversations"
expect "meta-unknown" "/tokens-nonsense" "" "Unknown meta command"

# 24-25. fetch over the local fixture server; multi-file tool.
sleep 1
expect "fetch-local" "/fetch http://127.0.0.1:$PORT text" "" "golden-fetch-marker"
expect "multi-file" "/files hello.txt notes.md" "" "Multi-file content"

# 26. list: read-only directory listing (new registry-native tool).
expect "list-current-dir" "list:." "" "hello.txt"

# 27-28. chat-only routes reach the AI interface and surface its failure
# honestly when no provider is running (offline mode, fails fast).
expect "email-not-file-injection" "email me@example.com" "" "AI service"
expect "at-reference-routes-chat" "@hello.txt summarize this" "" "AI service"

printf '\n%d/%d golden routing cases passed\n' "$((CASES - FAILURES))" "$CASES"
if [ "$FAILURES" -ne 0 ]; then exit 1; fi
