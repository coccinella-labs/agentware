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
trap 'rm -rf "$WORK" "$PROV" "$OUTSIDE" "$ABS_OUT" "$SPECIAL_DIR"' EXIT
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
  local tool_timeout="${TOOL_TIMEOUT_SEC:-}"
  CASES=$((CASES + 1))
  printf '%s' "$replies" > "$PROV/tl_replies.json"
  MOCK_RESPONSES="$PROV/tl_replies.json" \
    python3 "$MOCK_PROVIDER" "$PORT" >"$PROV/tl_prov.log" 2>&1 &
  local ppid=$!
  sleep 1
  local stdin_lines actual
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  local -a env_args=(
    AGENTWARE_TOOL_CALL_LOOP=1
    TEST_MODE=1
    "TEST_OLLAMA_URL=http://127.0.0.1:${PORT}/api/chat"
  )
  if [ -n "$tool_timeout" ]; then
    env_args+=("AGENTWARE_TOOL_TIMEOUT_SEC=$tool_timeout")
  fi
  actual=$(printf '%b' "$stdin_lines" | \
    env "${env_args[@]}" "$BIN" 2>&1 |
    perl -pe 's/\e\[[0-9;]*[mK]//g')
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

# Asserts the output shows a refusal (unknown tool, read-only refusal,
# model-surface refusal, or a no-TTY consent denial) and never a success
# marker.
expect_refused() {
  local name="$1" replies="$2" input="$3" success="$4" extra="${5:-}"
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
  local refused=0
  echo "$actual" | grep -qE "unknown tool |not available in read-only mode|not available on the model surface|consent denied: no TTY" && refused=1
  if [ "$refused" -eq 0 ]; then
    echo "FAIL  $name (no refusal marker in output)"
    echo "$actual" | sed 's/^/      | /' | head -14
    FAILURES=$((FAILURES + 1))
  elif echo "$actual" | grep -qF "$success"; then
    echo "FAIL  $name (executed: found '$success')"
    echo "$actual" | sed 's/^/      | /' | head -14
    FAILURES=$((FAILURES + 1))
  else
    echo "PASS  $name"
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

# 29. git:diff, todos: and analyze: are not model-callable in v1: their
#     services do not filter secret paths yet.
for t in 'git:diff' 'todos' 'analyze'; do
  replies=$(python3 -c "
import json, sys
name = sys.argv[1]
print(json.dumps([json.dumps({'tool': name,
                              'arguments': {'subcommand': 'diff',
                                            'path': '.'}}),
                  'Done.']))
" "$t")
  run_case "model-disabled-$t" "$replies" "run it" \
    "not available on the model surface"
done

# The same refusal when git:diff is named with a subcommand argument
# instead of the colon form.
run_case "model-disabled-git-subcommand" \
  '["{\"tool\":\"git\",\"arguments\":{\"subcommand\":\"diff\",\"path\":\".\"}}","Done."]' \
  "run it" "not available on the model surface"

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

# 31. Variant sweep: every mutating or model-disabled tool, named in a
#     variety of forms, must be refused or reported as an unknown tool.
#     No variant may execute.
sweep_case() {
  local label="$1" tool="$2" args="$3" success="$4"
  local replies
  replies=$(python3 -c "
import json, sys
name = sys.argv[1]
print(json.dumps([json.dumps({'tool': name,
                              'arguments': json.loads(sys.argv[2])}),
                  'Done.']))
" "$tool" "$args")
  expect_refused "sweep-$label" "$replies" "do it" "$success"
}

# Mutating tools: cmd:, write:, replace:, remember:, clear, forget.
for t in "cmd:" "write:" "replace:" "remember:" "clear" "forget"; do
  base="${t%:}"
  for v in "$t" "${t%:}:" "$(printf '%s' "$t" | tr '[:lower:]' '[:upper:]')" \
           " $t" "$t " "!$t" "/$t"; do
    sweep_case "$(printf '%s' "$v" | tr -c 'A-Za-z0-9' '_')" "$v" '{}' \
      "Sandboxed"
  done
done

# Model-disabled tools: todos:, analyze:, and git:diff.
for t in "todos:" "analyze:"; do
  for v in "$t" "$(printf '%s' "$t" | tr '[:lower:]' '[:upper:]')" \
           " $t" "$t " "!$t" "/$t"; do
    sweep_case "$(printf '%s' "$v" | tr -c 'A-Za-z0-9' '_')" "$v" '{}' \
      "Found "
  done
done
# git:diff in both the colon form and the argument form.
for v in "git:diff" "GIT:DIFF" " git:diff" "git:diff " "!git:diff" "/git:diff"; do
  sweep_case "$(printf '%s' "$v" | tr -c 'A-Za-z0-9' '_')" "$v" '{}' \
    "diff --git"
done
sweep_case "git-diff-arg" "git" '{"subcommand":"diff","path":"."}' "diff --git"
sweep_case "git-diff-arg-upper" "GIT" '{"subcommand":"DIFF","path":"."}' "diff --git"
sweep_case "git-diff-arg-space" "git" '{"subcommand":" diff ","path":"."}' "diff --git"

# Consent-gated tools: fetch: and github:. Every variant must still be
# consent-gated. The success marker is the argument the tool would carry
# into dispatch, which only appears when the consent gate was skipped.
for v in "fetch:" "fetch" "FETCH:" " fetch:" "fetch: " "!fetch:" "/fetch:"; do
  sweep_case "$(printf '%s' "$v" | tr -c 'A-Za-z0-9' '_')" "$v" \
    '{"url":"http://127.0.0.1:1/"}' "127.0.0.1:1"
done
for v in "github:" "github" "GITHUB:" " github:" "github: " "!github:" "/github:"; do
  sweep_case "$(printf '%s' "$v" | tr -c 'A-Za-z0-9' '_')" "$v" \
    '{"params":"repo:o/r"}' "repo:o/r"
done

# Zero-width and Unicode lookalikes in a tool name.
sweep_case "zero-width" "$(printf 'wri\u200bte')" '{}' "Tool write"
sweep_case "zero-width-join" "$(printf 'wri\u200dte')" '{}' "Tool write"
sweep_case "cyrillic-lookalike" "$(printf '\u0441rite')" '{}' "Tool write"
sweep_case "fullwidth" "$(printf '\uff57rite')" '{}' "Tool write"
sweep_case "nbsp-write" "$(printf 'write\u00a0')" '{}' "Tool write"

# 32. Special targets for read:. None may return file content.
SPECIAL_DIR="$(mktemp -d)"
# /dev and /proc reached through a symlink that lives INSIDE cwd: the
# canonical path leaves cwd, so the containment check must refuse them.
ln -sfn /proc/self/environ "$WORK/proclink" 2>/dev/null || true
ln -sfn /dev/zero "$WORK/devzero" 2>/dev/null || true
printf 'plain content\n' > "$SPECIAL_DIR/plain.txt"

# 32a. A file:// URL is not a path: it must not be read as one.
read_url=$(python3 -c "
import json
print(json.dumps([json.dumps({'tool': 'read',
                              'arguments': {'filename': 'file://$SPECIAL_DIR/plain.txt'}}),
                  'Done.']))
")
run_case "read-file-url" "$read_url" "read the url" "Error"

# 32b. A FIFO INSIDE cwd: opening it blocks forever, so the per-tool
#      timeout must fire and the model must be told, not left hanging.
#      (A FIFO outside cwd is caught by the containment check instead;
#      that case is covered below.)
mkfifo "$WORK/myfifo" 2>/dev/null || true
read_fifo=$(python3 -c "
import json, sys
print(json.dumps([json.dumps({'tool': 'read',
                              'arguments': {'filename': sys.argv[1]}}),
                  'Done.']))
" "myfifo")
TOOL_TIMEOUT_SEC=1 run_case "read-fifo-timeout" "$read_fifo" "read the fifo" \
  "tool timed out after 1s"

# 32c. /dev reached through a symlink inside cwd: the canonical path
#      leaves cwd, so the containment check refuses it.
replies=$(python3 -c "
import json
print(json.dumps([json.dumps({'tool': 'read',
                              'arguments': {'filename': 'devzero'}}),
                  'Done.']))
")
run_case "read-dev-symlink" "$replies" "read the special path" \
  "Path escapes the permitted root"

# /proc reached through a symlink inside cwd. On a host without /proc
# the symlink is dangling, so the refusal is "does not exist" instead of
# "escapes the permitted root"; either way nothing is read.
replies=$(python3 -c "
import json
print(json.dumps([json.dumps({'tool': 'read',
                              'arguments': {'filename': 'proclink'}}),
                  'Done.']))
")
run_case "read-proc-symlink" "$replies" "read the special path" "Error"

# 32d. fetch: with a file:// URL. On the model surface the consent gate
#      fires first (no TTY), so the file:// URL is never requested. The
#      URL validation itself (file:// is not http/https) is covered by the
#      WebService unit test.
fetch_url=$(python3 -c "
import json
print(json.dumps([json.dumps({'tool': 'fetch',
                              'arguments': {'url': 'file:///etc/passwd'}}),
                  'Done.']))
")
run_case "fetch-file-url-refused" "$fetch_url" "fetch a file url" \
  "consent denied: no TTY"

printf '\n%d/%d tool-loop golden cases passed\n' "$((CASES - FAILURES))" "$CASES"
if [ "$FAILURES" -ne 0 ]; then exit 1; fi
