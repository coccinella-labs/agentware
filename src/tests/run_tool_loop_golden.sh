#!/usr/bin/env bash
# Hermetic golden tests for the model tool-call loop (PR 2).
# Requires AGENTWARE_TOOL_CALL_LOOP=1 and a running mock tool provider.
set -euo pipefail

BIN="${1:-./bin/agentware-agent}"
PORT="${PORT:-11499}"
FAILURES=0
CASES=0

run_case() {
  local name="$1" replies="$2" input="$3" expect="$4" extra="${5:-}"
  CASES=$((CASES + 1))
  printf '%s' "$replies" > /tmp/tl_replies.json
  MOCK_RESPONSES=/tmp/tl_replies.json python3 src/tests/mock_tool_provider.py "$PORT" >/tmp/tl_prov.log 2>&1 &
  local ppid=$!
  sleep 1
  local stdin_lines actual
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  actual=$(printf '%b' "$stdin_lines" | "$BIN" 2>&1 |
    perl -pe 's/\e\[[0-9;]*[mK]//g')
  kill $ppid 2>/dev/null || true
  wait $ppid 2>/dev/null || true
  if echo "$actual" | grep -qF "$expect"; then
    echo "PASS  $name"
  else
    echo "FAIL  $name (expected '$expect')"
    echo "$actual" | sed 's/^/      | /' | head -12
    FAILURES=$((FAILURES + 1))
  fi
}

# 1. Single valid tool call executes and feeds observation back.
run_case "single-read" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","Done."]' \
  "list the current directory" "entries"

# 2. Malformed JSON is a recoverable error, not a crash.
run_case "malformed" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\".\"","Done."]' \
  "list the current directory" "malformed"

# 3. Unknown tool name is reported with available list.
run_case "unknown-tool" \
  '["{\"tool\":\"nope\",\"arguments\":{}}","Done."]' \
  "do something" "unknown tool nope"

# 4. Two calls in one reply is rejected.
run_case "multi-call" \
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}\n{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","Done."]' \
  "list twice" "multiple tool calls"

# 5. Prose + JSON is treated as a final answer (no execution).
run_case "prose-json" \
  '["Here is a call: {\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","Done."]' \
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
  '["{\"tool\":\"list\",\"arguments\":{\"path\":\".\"},}","Done."]' \
  "list the dir" "malformed tool call: invalid JSON"

# 10. Absolute path outside cwd is refused (model surface).
run_case "abs-path-outside-cwd" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\"/etc/passwd\"}}","Done."]' \
  "read passwd" "Path escapes the permitted root"

# 11. Secrets file inside cwd is refused.
run_case "secrets-env" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\".env\"}}","Done."]' \
  "read env" "Refused: secrets path"

# 12. fetch: from the model requires consent (exfiltration channel).
run_case "fetch-needs-consent" \
  '["{\"tool\":\"fetch\",\"arguments\":{\"url\":\"http://127.0.0.1:18093\"}}","Done."]' \
  "fetch page" "Command cancelled by user" "n"

# 13. Symlink inside cwd pointing outside is refused (canonical containment).
#     Create the symlink fixture (idempotent).
ln -sfn /tmp /tmp/tl_escape_link 2>/dev/null || true
mkdir -p /tmp/tl_outside && echo secret > /tmp/tl_outside/secret.txt
rm -f tl_escape_link
ln -s /tmp/tl_outside tl_escape_link
run_case "symlink-escape"   '["{\"tool\":\"read\",\"arguments\":{\"filename\":\"tl_escape_link/secret.txt\"}}","Done."]'   "read escaped" "Path escapes the permitted root"
rm -f tl_escape_link

# 14. Per-turn cap (8) trips on the 9th consecutive tool call.
run_case "turn-cap-trip"   '["{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","{\"tool\":\"list\",\"arguments\":{\"path\":\".\"}}","Done."]'   "list many" "Tool turn cap (8) reached"

# Note: per-run budget trip, per-tool timeout, and ~/.aws/* / .npmrc secrets
# paths are covered by unit-level reasoning but not by deterministic golden
# cases here (per-run budget is unreachable under the 4000-char per-turn
# truncation; per-tool timeout needs a slow command). They are tracked as
# follow-ups.

# 13b. Per-turn truncation: reading a ~10 KB file yields the truncation marker.
run_case "per-turn-truncation" \
  '["{\"tool\":\"read\",\"arguments\":{\"filename\":\"big.txt\"}}","Done."]' \
  "read big file" "observation truncated at 4000 chars"

# 14-19. Meta/slash/bang commands are not tools: unknown tool + available list.
for t in '/auth' '/chat save' '/checkpoint' '/mcp' '/sandbox' '!echo'; do
  replies=$(printf '[{"tool":%s,"arguments":{}},"Done."]' "$(python3 -c "import json,sys; print(json.dumps(sys.argv[1]))" "$t")")
  run_case "unknown-meta-$t" "$replies" "do it" "unknown tool $t"
done

printf '\n%d/%d tool-loop golden cases passed\n'  "$((CASES - FAILURES))" "$CASES"
if [ "$FAILURES" -ne 0 ]; then exit 1; fi
EOF
chmod +x src/tests/run_tool_loop_golden.sh