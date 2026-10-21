#!/usr/bin/env bash
# Golden tests for the model tool-call loop (PR 2).
#
# Each case runs the built binary in a hermetic temp workspace against a
# mock provider that serves canned assistant replies, and asserts the
# observable markers in stdout. The mock is a local Ollama-compatible
# HTTP server (src/tests/mock_tool_provider.py); no external network.
#
# Usage: run_tool_loop_golden.sh <path-to-agentware-agent>
# Needs bash, git, python3, and standard POSIX tools.
set -u

BIN="${1:?usage: run_tool_loop_golden.sh <agentware-agent binary>}"
PORT="${PORT:-0}"
if [ "$PORT" = "0" ]; then
  for p in 11499 11500 11501 11502 11503; do
    if ! (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null; then PORT=$p; break; fi
  done
fi
: "${PORT:=11499}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
MOCK_PROVIDER="${MOCK_PROVIDER:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/mock_tool_provider.py}"
WORK="$(mktemp -d)"
PROV="$(mktemp -d)"
trap 'rm -rf "$WORK" "$PROV" "$OUTSIDE" "$ABS_OUT"' EXIT
cd "$WORK" || exit 1

export HOME="$WORK"
export USERPROFILE="$WORK"
unset SERPAPI_KEY

FAILURES=0
CASES=0

# Fixtures: a tiny tree so tool output is small and stable.
printf 'golden needle alpha\nsecond line\n' > hello.txt
printf 'notes file with needle words\n' > notes.md
printf 'API_KEY=supersecret\n' > .env
printf 'API_KEY=supersecret\n' > id_rsa
mkdir -p tl_fixture
printf 'one\n' > tl_fixture/one.txt
printf 'two\n' > tl_fixture/two.txt
printf 'needle} say "hi"\n' > tl_fixture/brace.txt

run_case() {
  local name="$1" replies="$2" input="$3" expect="$4" extra="${5:-}"
  CASES=$((CASES + 1))
  printf '%s' "$replies" > "$PROV/tl_replies.json"
  MOCK_RESPONSES="$PROV/tl_replies.json" \
    python3 "$MOCK_PROVIDER" "$PORT" >"$PROV/tl_prov.log" 2>&1 &
  local ppid=$!
  sleep 1
  local stdin_lines actual
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  actual=$(printf '%b' "$stdin_lines" | \
    AGENTWARE_TOOL_CALL_LOOP=1 \
    TEST_MODE=1 \
    TEST_OLLAMA_URL="http://127.0.0.1:${PORT}/api/chat" \
    "$BIN" 2>&1 | perl -pe 's/\e\[[0-9;]*[mK]//g')
  kill $ppid 2>/dev/null || true
  wait $ppid 2>/dev/null || true
  if echo "$actual" | grep -qF "$expect"; then
    echo "PASS  $name"
  else
    echo "FAIL  $name (expected '$expect')"
    echo "$actual" | sed 's/^/      | /' | head -14
    FAILURES=$((FAILURES + 1))
  fi
}

# Same setup, but asserts a marker is ABSENT (nothing executed).
expect_no() {
  local name="$1" replies="$2" input="$3" absent="$4" extra="${5:-}"
  CASES=$((CASES + 1))
  printf '%s' "$replies" > "$PROV/tl_replies.json"
  MOCK_RESPONSES="$PROV/tl_replies.json" \
    python3 "$MOCK_PROVIDER" "$PORT" >"$PROV/tl_prov.log" 2>&1 &
  local ppid=$!
  sleep 1
  local stdin_lines actual
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  actual=$(printf '%b' "$stdin_lines" | \
    AGENTWARE_TOOL_CALL_LOOP=1 \
    TEST_MODE=1 \
    TEST_OLLAMA_URL="http://127.0.0.1:${PORT}/api/chat" \
    "$BIN" 2>&1 | perl -pe 's/\e\[[0-9;]*[mK]//g')
  kill $ppid 2>/dev/null || true
  wait $ppid 2>/dev/null || true
  if echo "$actual" | grep -qF "$absent"; then
    echo "FAIL  $name (unexpected '$absent')"
    echo "$actual" | sed 's/^/      | /' | head -14
    FAILURES=$((FAILURES + 1))
  else
    echo "PASS  $name"
  fi
}

# 1. Single valid tool call executes and feeds observation back.
run_case "single-read" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","Done."]' \
  "list the fixture directory" "one.txt"

# 2. Malformed JSON is a recoverable error, not a crash.
run_case "malformed" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"","Done."]' \
  "list the fixture directory" "malformed"

# 3. Unknown tool name is reported with available list.
run_case "unknown-tool" \
  '["{\"tool\":\"nope\",\"arguments\":{}}","Done."]' \
  "do something" "unknown tool nope"

# 4. Two JSON objects in one message: not one JSON object, so it is a
#    final answer. Nothing executes.
expect_no "multi-call-not-executed" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}\n{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","Done."]' \
  "list twice" "Tool list"

# 5. Prose + JSON is treated as a final answer (no execution).
run_case "prose-json" \
  '["Here is a call: {\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","Done."]' \
  "list the dir" "Here is a call"

# 6. Mutating tool is refused, never prompts.
run_case "mutating-refused" \
  '["{\"tool\":\"write\",\"arguments\":{\"filename\":\"x.txt\",\"content\":\"hi\"}}","Done."]' \
  "write a file" "not available in read-only mode"

# 7. Completion signal done ends the loop.
run_case "done-signal" \
  '["{\"done\":true,\"summary\":\"all good\"}"]' \
  "summarize" "all good"

# 8. Completion signal needs_user_input surfaces the question.
run_case "needs-input" \
  '["{\"needs_user_input\":\"which file?\"}"]' \
  "edit something" "which file?"

# 9. Trailing comma is malformed JSON, recoverable.
run_case "trailing-comma" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"},}","Done."]' \
  "list the dir" "malformed tool call: invalid JSON"

# 10. Absolute path outside cwd is refused (model surface). The target is
#     deliberately NOT a restricted system path: the read handler refuses
#     /etc/passwd on its own, so that path would not exercise the
#     model-surface containment check.
ABS_OUT="$(mktemp -d)"
echo outside > "$ABS_OUT/outside.txt"
# The escaped target for the tilde/$HOME cases.
echo escaped > "$ABS_OUT/../escaped.txt" 2>/dev/null || true
replies=$(python3 -c "
import json, sys
print(json.dumps([json.dumps({'tool': 'read',
                              'arguments': {'filename': sys.argv[1]}}),
                  'Done.']))
" "$ABS_OUT/outside.txt")
run_case "abs-path-outside-cwd" "$replies" \
  "read outside" "Path escapes the permitted root"

# 10b. ~ and $HOME are expanded BEFORE the containment check. The
#      "..\" after the expansion escapes cwd. Without expansion the
#      literal path resolves inside cwd ("<cwd>/~/../escaped.txt" ->
#      "<cwd>/escaped.txt") and would be allowed, so these cases prove
#      the expansion happens first.
run_case "tilde-expanded-refused" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\"~/../escaped.txt\"}}","Done."]' \
  "read tilde escape" "Path escapes the permitted root"

run_case "home-var-expanded-refused" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\"$HOME/../escaped.txt\"}}","Done."]' \
  "read home escape" "Path escapes the permitted root"

# 11. Secrets file inside cwd is refused.
run_case "secrets-env" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\".env\"}}","Done."]' \
  "read env" "Refused: secrets path"

# 12. The consent-prompt path needs a TTY. The harness always pipes, so the
#     observable behavior is the no-TTY denial (covered by the next case).
#     A TTY-driven consent case needs a pty harness and is not covered here.

# 13. fetch: from the model is denied without a TTY (no consent read from pipe).
run_case "fetch-no-tty" \
  '["{\"tool\":\"fetch\",\"arguments\":{\"url\":\"http://127.0.0.1:1/\"}}","Done."]' \
  "fetch page" "consent denied: no TTY"

# Even piping "y" on stdin does not grant consent (no TTY).
run_case "fetch-no-tty-y" \
  '["{\"tool\":\"fetch\",\"arguments\":{\"url\":\"http://127.0.0.1:1/\"}}","Done."]' \
  "fetch page" "consent denied: no TTY" "y"

run_case "github-no-tty-y" \
  '["{\"tool\":\"github\",\"arguments\":{\"params\":\"repo:o/r\"}}","Done."]' \
  "github repo" "consent denied: no TTY" "y"

# 14. Per-turn truncation: reading a ~10 KB file yields the truncation marker.
printf 'x%.0s' $(seq 1 10000) > big.txt
run_case "per-turn-truncation" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\"big.txt\"}}","Done."]' \
  "read big file" "observation truncated at 4000 chars"

# 15. Symlink inside cwd pointing outside is refused (canonical containment).
OUTSIDE="$(mktemp -d)"
echo payload > "$OUTSIDE/payload.txt"
ln -s "$OUTSIDE" escape_link
run_case "symlink-escape" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\"escape_link/secret.txt\"}}","Done."]' \
  "read escaped" "Path escapes the permitted root"

# 16. Per-turn cap (8) trips on the 9th consecutive tool call.
run_case "turn-cap-trip" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}","Done."]' \
  "list many" "Tool turn cap (8) reached"

# 17. grep must not leak secrets from a secrets file even when it matches.
#     The file is NOT hidden: ripgrep skips hidden files by default, so a
#     hidden fixture would never be searched and the case would be vacuous.
run_case "grep-no-secret-leak" \
  '["{\"tool\":\"grep\",\"arguments\":{\"pattern\":\"API_KEY\",\"directory\":\".\"}}","Done."]' \
  "grep api key" "No matches found"

# 18-19. Meta/slash/bang commands are not tools: unknown tool + available list.
for t in '/auth' '/chat save' '/checkpoint' '/mcp' '/sandbox' '!echo'; do
  replies=$(python3 -c "
import json, sys
name = sys.argv[1]
print(json.dumps([json.dumps({'tool': name, 'arguments': {}}), 'Done.']))
" "$t")
  run_case "unknown-meta-$t" "$replies" "do it" "unknown tool $t"
done

# 19. Nested arguments object: one JSON object, recognized and executed.
run_case "nested-arguments" \
  '["{\"tool\":\"grep\",\"arguments\":{\"pattern\":\"needle\",\"directory\":\".\"}}","Done."]' \
  "grep for needle" "hello.txt"

# 20. A closing brace inside a string value must not end the object.
run_case "brace-inside-string" \
  '["{\"tool\":\"grep\",\"arguments\":{\"pattern\":\"needle}\",\"directory\":\".\"}}","Done."]' \
  "grep for needle}" "brace.txt"

# 21. Escaped quotes inside a string value must not end the object.
run_case "escaped-quotes" \
  '["{\"tool\":\"grep\",\"arguments\":{\"pattern\":\"say \\\"hi\\\"\",\"directory\":\".\"}}","Done."]' \
  "grep for say hi" "brace.txt"

# 22. Trailing text after a complete object: not one JSON object, so it is
#     a final answer and nothing executes.
expect_no "trailing-text-not-executed" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}} and that is it","Done."]' \
  "list the dir" "Tool list"

# 23. A JSON array is not a JSON object: final answer, nothing executes.
expect_no "array-not-executed" \
  '["[{\"tool\":\"list\",\"arguments\":{\"path\":\"tl_fixture\"}}]","Done."]' \
  "list the dir" "Tool list"

# 30. An oversized assistant message is refused before parsing.
oversized=$(python3 -c "
import json
big = 'x' * 70000
print(json.dumps([json.dumps({'tool': 'list',
                              'arguments': {'path': 'tl_fixture'},
                              'pad': big}),
                  'Done.']))
")
run_case "oversized-reply" "$oversized" \
  "list the dir" "assistant message too large"

printf '\n%d/%d tool-loop golden cases passed\n' "$((CASES - FAILURES))" "$CASES"
if [ "$FAILURES" -ne 0 ]; then exit 1; fi
