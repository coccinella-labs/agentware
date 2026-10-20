#!/usr/bin/env bash
# Hermetic golden tests for the model tool-call loop (PR 2).
# Requires AGENTWARE_TOOL_CALL_LOOP=1 and a running mock tool provider.
# Each case writes a canned reply list, starts the provider, runs the
# binary, and checks the output for an expected marker.
set -euo pipefail

BIN="${1:-./bin/agentware-agent}"
PORT="${PORT:-11499}"
FAILURES=0
CASES=0

run_case() {
  local name="$1" replies="$2" input="$3" expect="$4"
  CASES=$((CASES + 1))
  printf '%s' "$replies" > /tmp/tl_replies.json
  MOCK_RESPONSES=/tmp/tl_replies.json python3 src/tests/mock_tool_provider.py "$PORT" >/tmp/tl_prov.log 2>&1 &
  local ppid=$!
  sleep 1
  local out
  out=$(AGENTWARE_TOOL_CALL_LOOP=1 TEST_MODE=1 TEST_OLLAMA_URL="http://127.0.0.1:${PORT}/api/chat" \
        "$BIN" --pipe <<< "$input" 2>&1 || true)
  kill $ppid 2>/dev/null || true
  wait $ppid 2>/dev/null || true
  if echo "$out" | grep -qF "$expect"; then
    echo "PASS  $name"
  else
    echo "FAIL  $name (expected '$expect')"
    echo "$out" | head -20
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

printf '\n%d/%d tool-loop golden cases passed\n' "$((CASES - FAILURES))" "$CASES"
if [ "$FAILURES" -ne 0 ]; then exit 1; fi
EOF
chmod +x src/tests/run_tool_loop_golden.sh