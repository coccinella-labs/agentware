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
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
MOCK_PROVIDER="${MOCK_PROVIDER:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/mock_tool_provider.py}"
# Scratch directories the script creates. They are declared up front
# because the EXIT trap removes them, and an interrupt can arrive
# before the later ones are assigned: under 'set -u' an unset variable
# would abort the trap itself, and the cleanup would be skipped.
WORK=""
PROV=""
OUTSIDE=""
ABS_OUT=""
SPECIAL_DIR=""
WORK="$(mktemp -d)"
PROV="$(mktemp -d)"
PROVIDER_PID=""
trap on_exit EXIT
cd "$WORK" || exit 1

export HOME="$WORK"
export USERPROFILE="$WORK"
unset SERPAPI_KEY

# Canary exported into the agentware process. Reading /proc/self/environ
# would echo it back, which the special-target cases must never do.
ENV_SENTINEL="tzq4-leak-canary-9x"
export ENV_SENTINEL

FAILURES=0
CASES=0

# Every provider process this harness started records its own PID in
# $PROV/pids. The record lives in the provider, not in the shell, so the
# leak check below still works when the harness loses track of a PID.
PROVIDER_PORT=""
PROVIDER_PID=""

# Startup trace. The agent writes it when AGENTWARE_STARTUP_TRACE points
# at a file; only the first case enables it, so the file holds one
# startup, and it is printed at the end of the suite because the scratch
# directory is removed by the EXIT trap.
STARTUP_TRACE="$PROV/startup_trace.log"
# Wall-clock bracket of the first case, filled in while the case runs.
SPAWNED_AT=""
FINISHED_AT=""

# Fixtures: a tiny tree so tool output is small and stable.
printf 'golden needle alpha\nsecond line\n' > hello.txt
printf 'notes file with needle words\n' > notes.md
printf 'API_KEY=supersecret\n' > .env
printf 'API_KEY=supersecret\n' > id_rsa
mkdir -p tl_fixture
printf 'one\n' > tl_fixture/one.txt
printf 'two\n' > tl_fixture/two.txt
printf 'needle} say "hi"\n' > tl_fixture/brace.txt

# Starts the mock provider and waits until it is actually serving.
#
# The provider takes an OS-assigned port and writes it to $PROV/port, so
# two runs can never collide on a fixed port. The wait proves the port
# accepts a connection before the agent is started: a provider that never
# came up has to show up as a provider failure, not as an unrelated
# timeout inside the binary.
#
# Sets PROVIDER_PORT and PROVIDER_PID as globals and returns 0, or
# returns 1 if the provider never came up. It is called directly, never
# in a command substitution: a command substitution runs it in a
# subshell, and the backgrounded provider would then be orphaned when
# that subshell exits, so the parent would never learn its PID and
# could never kill it.
start_provider() {
  rm -f "$PROV/port" "$PROV/port.tmp"
  MOCK_RESPONSES="$PROV/tl_replies.json" \
  MOCK_PROVIDER_PORT_FILE="$PROV/port" \
  MOCK_PROVIDER_PID_FILE="$PROV/pids" \
    python3 "$MOCK_PROVIDER" >"$PROV/tl_prov.log" 2>&1 &
  local pid=$!
  PROVIDER_PID="$pid"
  local i port
  for i in $(seq 1 200); do
    if [ -s "$PROV/port" ]; then
      port=$(tr -cd '0-9' < "$PROV/port")
      if [ -n "$port" ] && (exec 3<>/dev/tcp/127.0.0.1/"$port") 2>/dev/null; then
        PROVIDER_PORT="$port"
        return 0
      fi
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      return 1
    fi
    sleep 0.1
  done
  return 1
}

stop_provider() {
  [ -n "$PROVIDER_PID" ] || return 0
  kill "$PROVIDER_PID" 2>/dev/null || true
  wait "$PROVIDER_PID" 2>/dev/null || true
  PROVIDER_PID=""
}

# Prints the PID of every provider this harness started that is still
# alive. The PIDs come from the providers' own records, not from a
# process search: a search would also match providers belonging to
# another run, and it would not know which processes this run started.
leaked_providers() {
  local pid
  [ -f "$PROV/pids" ] || return 0
  while read -r pid; do
    [ -n "$pid" ] || continue
    if kill -0 "$pid" 2>/dev/null; then
      echo "$pid"
    fi
  done < "$PROV/pids"
}

# Fails the suite if any provider outlived its case.
check_no_leaked_providers() {
  local leaked
  leaked=$(leaked_providers)
  if [ -n "$leaked" ]; then
    echo "LEAKED PROVIDERS (still alive after their case): $leaked"
    return 1
  fi
  return 0
}

# The EXIT trap. It stops the provider that is still running, checks
# that nothing leaked, and keeps the suite's exit status unless a leak
# was found.
on_exit() {
  local status=$?
  stop_provider
  if ! check_no_leaked_providers; then
    status=1
  fi
  local dir
  for dir in "$WORK" "$PROV" "$OUTSIDE" "$ABS_OUT" "$SPECIAL_DIR"; do
    [ -n "$dir" ] && rm -rf "$dir"
  done
  exit $status
}

# Prints the provider log under a failing case. Without it, "the provider
# was never reachable" and "the provider was reached but never answered"
# are indistinguishable from the outside, which is how a provider-side
# failure once hid behind a wall of 30 s connect timeouts.
dump_provider_log() {
  echo "      | --- mock provider log ---"
  sed 's/^/      | /' "$PROV/tl_prov.log" 2>/dev/null | head -30
}

# Reports a provider that never came up, and counts the case failed.
provider_failed() {
  local name="$1"
  echo "FAIL  $name (mock provider never accepted a connection)"
  dump_provider_log
  stop_provider
  FAILURES=$((FAILURES + 1))
}

run_case() {
  local name="$1" replies="$2" input="$3" expect="$4" extra="${5:-}"
  local tool_timeout="${TOOL_TIMEOUT_SEC:-}"
  CASES=$((CASES + 1))
  printf '%s' "$replies" > "$PROV/tl_replies.json"
  if ! start_provider; then
    provider_failed "$name"
    return
  fi
  local stdin_lines actual
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  local -a env_args=(
    AGENTWARE_TOOL_CALL_LOOP=1
    TEST_MODE=1
    no_proxy=127.0.0.1,localhost
    NO_PROXY=127.0.0.1,localhost
    # The agent appends the endpoint path to this variable itself, so
    # this is a BASE url. Passing the full endpoint here produced the
    # doubled path /api/chat/api/chat, which the mock answered anyway
    # because it serves any POST path.
    "TEST_OLLAMA_URL=http://127.0.0.1:${PROVIDER_PORT}"
    # A stalled case must cost seconds, not the 30 s network default.
    "AGENTWARE_NET_TIMEOUT_SEC=${NET_TIMEOUT_SEC:-5}"
  )
  if [ -n "$tool_timeout" ]; then
    env_args+=("AGENTWARE_TOOL_TIMEOUT_SEC=$tool_timeout")
  fi
  if [ "$CASES" -eq 1 ]; then
    env_args+=("AGENTWARE_STARTUP_TRACE=$STARTUP_TRACE")
  fi
  if [ "$CASES" -eq 1 ]; then
    # Brackets the whole process, including the exec and anything that
    # happens before main. The trace inside the agent cannot see that.
    SPAWNED_AT=$(date '+%H:%M:%S')
  fi
  actual=$(printf '%b' "$stdin_lines" | \
    env "${env_args[@]}" "$BIN" 2>&1 |
    perl -pe 's/\e\[[0-9;]*[mK]//g')
  if [ "$CASES" -eq 1 ]; then
    FINISHED_AT=$(date '+%H:%M:%S')
  fi
  stop_provider
  if echo "$actual" | grep -qF "$expect"; then
    echo "PASS  $name"
  else
    echo "FAIL  $name (expected '$expect')"
    echo "$actual" | sed 's/^/      | /' | head -14
    dump_provider_log
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
  if ! start_provider; then
    provider_failed "$name"
    return
  fi
  local stdin_lines actual
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  TRACE_ENV=""
  if [ "$CASES" -eq 1 ]; then
    TRACE_ENV="AGENTWARE_STARTUP_TRACE=$STARTUP_TRACE"
  fi
  actual=$(printf '%b' "$stdin_lines" | \
    AGENTWARE_TOOL_CALL_LOOP=1 \
    TEST_MODE=1 \
    no_proxy=127.0.0.1,localhost \
    NO_PROXY=127.0.0.1,localhost \
    TEST_OLLAMA_URL="http://127.0.0.1:${PROVIDER_PORT}" \
    AGENTWARE_NET_TIMEOUT_SEC="${NET_TIMEOUT_SEC:-5}" \
    ${TRACE_ENV:-} \
    "$BIN" 2>&1 | perl -pe 's/\e\[[0-9;]*[mK]//g')
  stop_provider
  local refused=0
  echo "$actual" | grep -qE "unknown tool |not available in read-only mode|not available on the model surface|consent denied: no TTY" && refused=1
  if [ "$refused" -eq 0 ]; then
    echo "FAIL  $name (no refusal marker in output)"
    echo "$actual" | sed 's/^/      | /' | head -14
    dump_provider_log
    FAILURES=$((FAILURES + 1))
  elif echo "$actual" | grep -qF "$success"; then
    echo "FAIL  $name (executed: found '$success')"
    echo "$actual" | sed 's/^/      | /' | head -14
    dump_provider_log
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
  if ! start_provider; then
    provider_failed "$name"
    return
  fi
  local stdin_lines actual
  stdin_lines=$(printf '2\n1\n%s\n%s\nquit\n' "$input" "$extra")
  TRACE_ENV=""
  if [ "$CASES" -eq 1 ]; then
    TRACE_ENV="AGENTWARE_STARTUP_TRACE=$STARTUP_TRACE"
  fi
  actual=$(printf '%b' "$stdin_lines" | \
    AGENTWARE_TOOL_CALL_LOOP=1 \
    TEST_MODE=1 \
    no_proxy=127.0.0.1,localhost \
    NO_PROXY=127.0.0.1,localhost \
    TEST_OLLAMA_URL="http://127.0.0.1:${PROVIDER_PORT}" \
    AGENTWARE_NET_TIMEOUT_SEC="${NET_TIMEOUT_SEC:-5}" \
    ${TRACE_ENV:-} \
    "$BIN" 2>&1 | perl -pe 's/\e\[[0-9;]*[mK]//g')
  stop_provider
  if echo "$actual" | grep -qF "$absent"; then
    echo "FAIL  $name (unexpected '$absent')"
    echo "$actual" | sed 's/^/      | /' | head -14
    dump_provider_log
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

# 30b. Colon form and argument form disagree: the call is ambiguous and
#      is refused rather than silently preferring one of them.
amb=$(python3 -c "
import json
print(json.dumps([
    json.dumps({'tool': 'git:log',
                'arguments': {'subcommand': 'diff', 'path': '.'}}),
    'Done.']))
")
run_case "ambiguous-colon-vs-arg" "$amb" "run it" "ambiguous tool call"

# 30c. The two forms AGREEING is not ambiguous: git:diff with the same
#      subcommand in the argument is still refused, but as a
#      model-surface refusal, not as an ambiguity.
agree=$(python3 -c "
import json
print(json.dumps([
    json.dumps({'tool': 'git:diff',
                'arguments': {'subcommand': 'diff', 'path': '.'}}),
    'Done.']))
")
run_case "agreeing-colon-and-arg" "$agree" "run it" \
  "not available on the model surface"

# 31. Variant sweep: every tool the model surface must refuse, named in a
#     variety of forms, must be refused or reported as an unknown tool. No
#     variant may execute.
#
#     The list comes from the tool registry (AGENTWARE_DUMP_TOOLS), so a
#     newly registered mutating or model-disabled tool is covered without
#     editing this script.
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

# The success marker is the argument the tool would carry into dispatch.
# If a variant is refused, the argument never reaches dispatch and the
# marker never appears.
sweep_args_for() {
  case "$1" in
    fetch:*) echo '{"url":"http://127.0.0.1:1/SWEEPMARKER"}' ;;
    github:*) echo '{"params":"repo:o/SWEEPMARKER"}' ;;
    git:*) echo '{"subcommand":"SWEEPSUB","path":"SWEEPMARKER"}' ;;
    *) echo '{"filename":"SWEEPMARKER","path":"SWEEPMARKER","fact":"SWEEPMARKER","content":"SWEEPMARKER","text":"SWEEPMARKER","old_text":"SWEEPMARKER","new_text":"SWEEPMARKER","command":"SWEEPMARKER","url":"http://127.0.0.1:1/SWEEPMARKER","params":"repo:o/SWEEPMARKER","directory":"SWEEPMARKER","query":"SWEEPMARKER","pattern":"SWEEPMARKER","subcommand":"SWEEPSUB"}' ;;
  esac
}

REGISTRY_DUMP="$(AGENTWARE_DUMP_TOOLS=1 TEST_MODE=1 "$BIN" 2>/dev/null || true)"
if [ -z "$REGISTRY_DUMP" ]; then
  echo "FATAL: could not read the tool registry dump" >&2
  exit 1
fi

# Every refused (tool, subcommand) pair, straight from the registry.
# Written to a file first: a pipeline would run the loop in a subshell and
# the case counters would be lost.
REFUSED_LIST="$PROV/refused.tsv"
printf '%s\n' "$REGISTRY_DUMP" | awk -F'\t' '$1=="refused" {print $2 "\t" $3}' > "$REFUSED_LIST"
while IFS="$(printf '\t')" read -r rtool rsub; do
  [ -n "$rtool" ] || continue
  rargs=$(sweep_args_for "$rtool")
  if [ -n "$rsub" ]; then
    rargs=$(printf '%s' "$rargs" | sed 's/SWEEPSUB/'"$rsub"'/')
  else
    rargs=$(printf '%s' "$rargs" | sed 's/SWEEPSUB/status/')
  fi
  # The colon form and the bare form, plus case, whitespace, shell/meta
  # prefixes and Unicode lookalikes.
  for v in "$rtool" "${rtool%:}" "$(printf '%s' "$rtool" | tr '[:lower:]' '[:upper:]')" \
           " $rtool" "$rtool " "!$rtool" "/$rtool"; do
    sweep_case "$(printf '%s' "$v" | tr -c 'A-Za-z0-9' '_')" "$v" "$rargs" \
      "SWEEPMARKER"
  done
done < "$REFUSED_LIST"

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
printf 'plain content\n' > "$WORK/plain.txt"

# 32a. A file:// URL is not a path: it must not be read as one.
read_url=$(python3 -c "
import json
print(json.dumps([json.dumps({'tool': 'read',
                              'arguments': {'filename': 'file://$SPECIAL_DIR/plain.txt'}}),
                  'Done.']))
")
run_case "read-file-url" "$read_url" "read the url" "Error"

# 32b. A FIFO INSIDE cwd is refused before it is opened: opening a FIFO
#      blocks forever, and a stuck handler holds the tool gate, which
#      would refuse every later call for the rest of the session.
#      The case therefore runs TWO turns: the FIFO read first, then a
#      normal read, and asserts the second turn still works.
mkfifo "$WORK/myfifo" 2>/dev/null || true
fifo_then_read=$(python3 -c "
import json
print(json.dumps([
    json.dumps({'tool': 'read', 'arguments': {'filename': 'myfifo'}}),
    json.dumps({'tool': 'read', 'arguments': {'filename': 'plain.txt'}}),
    'Done.']))
")
run_case "read-fifo-refused" "$fifo_then_read" "read the fifo" \
  "Refusing to read a FIFO"
run_case "read-after-fifo-works" "$fifo_then_read" "read the fifo" \
  "plain content"

# 32b-2. A FIFO inside the workspace must not hang the directory walkers
#        (grep:, list:, tree:) and must not leave a stuck handler: a later
#        call in the SAME session still works. analyze: is refused on the
#        model surface, but it must not leave a stuck handler either.
walker_then_read() {
  local tool="$1" args="$2" label="$3"
  local replies
  replies=$(python3 -c "
import json, sys
tool, args = sys.argv[1], sys.argv[2]
print(json.dumps([
    json.dumps({'tool': tool, 'arguments': json.loads(args)}),
    json.dumps({'tool': 'read', 'arguments': {'filename': 'plain.txt'}}),
    'Done.']))
" "$tool" "$args")
  # The second turn reads plain.txt: if the first turn left a stuck
  # handler, the tool gate refuses it and the read never happens.
  run_case "walker-$label-then-read" "$replies" "walk the dir" "plain content"
}

mkfifo "$WORK/myfifo" 2>/dev/null || true
walker_then_read grep '{"pattern":"needle","directory":"."}' "grep"
walker_then_read list '{"path":"."}' "list"
walker_then_read tree '{"path":"."}' "tree"
walker_then_read analyze '{"path":"."}' "analyze"

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

# /proc reached through a symlink inside cwd. The refusal depends on the
# host: where /proc exists the link resolves, the canonical path leaves
# cwd, and the containment check refuses it ("Path escapes the permitted
# root"); where /proc is absent the link is dangling and the refusal is a
# not-found error. Either way nothing is read, so the expected refusal
# follows whether the link resolves.
replies=$(python3 -c "
import json
print(json.dumps([json.dumps({'tool': 'read',
                              'arguments': {'filename': 'proclink'}}),
                  'Done.']))
")
if [ -e "$WORK/proclink" ]; then
  proc_refusal="Path escapes the permitted root"
else
  proc_refusal="Error"
fi
run_case "read-proc-symlink" "$replies" "read the special path" "$proc_refusal"

# The same read must never return file content. The sentinel is exported
# into the agentware process, so a successful read of /proc/self/environ
# would echo it back; this assertion is the same on every host.
expect_no "read-proc-symlink-no-leak" "$replies" "read the special path" \
  "$ENV_SENTINEL"

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

# No provider may outlive its case. This runs before the EXIT trap, and
# the trap runs the same check again, so a leak is reported on both the
# normal path and the interrupted one.
if ! check_no_leaked_providers; then
  FAILURES=$((FAILURES + 1))
fi

if [ -s "$STARTUP_TRACE" ]; then
  echo ""
  echo "--- startup trace (first case) ---"
  echo "      | agent spawned at ${SPAWNED_AT}, finished at ${FINISHED_AT}"
  sed 's/^/      | /' "$STARTUP_TRACE"
fi

printf '\n%d/%d tool-loop golden cases passed\n' "$((CASES - FAILURES))" "$CASES"

if [ "$FAILURES" -ne 0 ]; then exit 1; fi
