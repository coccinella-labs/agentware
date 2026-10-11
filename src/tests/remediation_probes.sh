#!/bin/bash
# Regression probes for docs/audit/remediation-plan.md (R-01..R-10).
# Drives the built agentware-agent binary over piped stdin in an isolated
# temp dir (the agent writes data/memory.txt relative to CWD) and asserts
# exit status plus expected output markers. No network is required:
# offline mode is selected and unreachable hosts fail fast.
# Usage: remediation_probes.sh <path-to-agentware-agent>
# Portability: needs bash, curl, python3, mktemp, and standard POSIX tools.
# Symlink and restricted-path cases adapt per OS (see HAVE_SYMLINK and
# RESTRICTED_ABS); build the binary under test for the host first.
set -u

BIN="${1:?usage: remediation_probes.sh <agentware-agent binary>}"
WORK="$(mktemp -d)"
trap 'kill "$SERVER_PID" 2>/dev/null; rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

# Hermetic key state: search must not depend on ambient SERPAPI_KEY.
unset SERPAPI_KEY

# Hermetic home: global memory must land in the temp dir, never in the
# operator's real ~/.agentware.
export HOME="$WORK"
export USERPROFILE="$WORK"

# Deterministic tool PATH: the stock probes must never see a real docker
# (routing depends on its absence), while python3/curl stay available.
# Fake-docker scenarios below prepend their stub dir explicitly.
BASE_PATH="/usr/bin:/bin:/usr/sbin:/sbin"
PROBE_PATH="$BASE_PATH"
export PATH="$BASE_PATH"

PASS=0
FAIL=0

# Fixture file for read:/write:/replace: probes (probes run in $WORK).
printf 'alpha\nbeta\ngamma\n' > sample.txt

# Large plain-text fixture (>1000 chars) for /fetch AI-path probes.
python3 -c "print('lorem ipsum dolor sit amet ' * 60)" > big.txt
# Large valid-JSON fixture (>1000 chars) for the /fetch json AI path.
python3 -c "import json; print(json.dumps({'data': 'x' * 2000}))" > big.json

# Local HTTP server for /fetch probes (no external network).
SERVE_PORT=18321
python3 -m http.server "$SERVE_PORT" --bind 127.0.0.1 > server.log 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null; rm -rf "$WORK"' EXIT
i=0
while [ "$i" -lt 50 ]; do
  if curl -sf -o /dev/null "http://127.0.0.1:$SERVE_PORT/sample.txt"; then
    break
  fi
  sleep 0.2
  i=$((i + 1))
done

# probe <name> <expected_exit> <expected_output_grep> [input lines...]
# Each input line is fed to the binary's stdin in order. The binary runs
# with PROBE_PATH so docker visibility is controlled per section.
probe() {
  local name="$1" expected_exit="$2" expected_grep="$3"
  shift 3
  local out exit_code
  out=$(printf '%s\n' "$@" | env "PATH=$PROBE_PATH" "$BIN" 2>&1)
  exit_code=$?
  # Strip ANSI color codes for stable matching.
  out=$(printf '%s' "$out" | sed 's/\x1b\[[0-9;]*m//g')
  if [ "$exit_code" -eq "$expected_exit" ] &&
    printf '%s' "$out" | grep -qF "$expected_grep"; then
    PASS=$((PASS + 1))
    echo "PASS: $name"
  else
    FAIL=$((FAIL + 1))
    echo "FAIL: $name (exit=$exit_code, want exit=$expected_exit, want '$expected_grep')"
    printf '%s\n' "$out" | tail -n 5
  fi
}

seed5() {
  # Five non-chat interactions: seed history without creating AI service.
  echo "read:sample.txt"
  echo "read:sample.txt"
  echo "read:sample.txt"
  echo "read:sample.txt"
  echo "read:sample.txt"
}

# probe_absent <name> <expected_exit> <must_not_appear> [input lines...]
probe_absent() {
  local name="$1" expected_exit="$2" absent="$3"
  shift 3
  local out exit_code
  out=$(printf '%s\n' "$@" | env "PATH=$PROBE_PATH" "$BIN" 2>&1)
  exit_code=$?
  out=$(printf '%s' "$out" | sed 's/\x1b\[[0-9;]*m//g')
  if [ "$exit_code" -eq "$expected_exit" ] &&
    ! printf '%s' "$out" | grep -qF "$absent"; then
    PASS=$((PASS + 1))
    echo "PASS: $name"
  else
    FAIL=$((FAIL + 1))
    echo "FAIL: $name (exit=$exit_code, want exit=$expected_exit, must not contain '$absent')"
    printf '%s\n' "$out" | tail -n 5
  fi
}

# R-03: /compress with history but no prior chat must not crash.
probe "R-03 compress without chat survives" 0 "AI service unavailable" \
  "2" "1" "$(seed5)" "/compress" "exit"

# R-01: malformed read: ranges must error, not crash.
probe "R-01 read non-numeric range errors" 0 "Error: invalid start line" \
  "2" "1" "read:sample.txt:abc" "exit"
probe "R-01 read non-numeric count errors" 0 "Error: invalid line range" \
  "2" "1" "read:sample.txt:1:xyz" "exit"
probe "R-01 read valid range still works" 0 "beta" \
  "2" "1" "read:sample.txt:1:1" "exit"

# A1: replace: with an empty pattern must error, not hang.
probe "A1 replace empty pattern errors" 0 "Error: old text cannot be empty" \
  "2" "1" "replace:sample.txt::new" "exit"

# A2: components: on a bad path must error, not kill the session.
probe "A2 components bad path errors" 0 "Error: cannot list components" \
  "2" "1" "components:/nonexistent_xyz_123" "exit"

# A3: github: API failures must error, not kill the session. A nonexistent
# repo fails deterministically in every environment (offline DNS failure
# or API 404 both throw inside the service); the exact message varies by
# environment, so assert the controlled-Error shape, not the text.
probe "A3 issues API failure errors" 0 "Error:" \
  "2" "1" "github:issues:no-such-owner-xyz/no-such-repo-xyz" "exit"
probe "A3 health API failure errors" 0 "Error:" \
  "2" "1" "github:health:no-such-owner-xyz/no-such-repo-xyz" "exit"

# B2: exact bare words route to their handlers; near-matches do not.
probe "B2 bare forget clears memory" 0 "Global memory cleared" \
  "2" "1" "remember:probefact" "forget" "exit"
probe "B2 bare memory reports cleared state" 0 "No global memories stored" \
  "2" "1" "remember:probefact" "forget" "memory" "exit"
probe "B2 bare clear wipes session" 0 "Session memory cleared" \
  "2" "1" "clear" "exit"
probe "B2 near-match leaves memory intact" 0 "probefact" \
  "2" "1" "remember:probefact" "memoryfoo" "memory" "forget" "exit"

# B1: git:log must show real log output, not trip on its own operators.
# The handler inspects hardcoded "." (the probe CWD), so make it a repo.
if command -v git >/dev/null 2>&1; then
  git init -q . && git add -A 2>/dev/null
  git -c user.email=t@t -c user.name=t commit -qm seed 2>/dev/null
  probe "B1 git log shows commits" 0 "seed" \
    "2" "1" "git:log" "exit"
fi

# R-02: malformed replace: count must error, not crash.
probe "R-02 replace non-numeric count errors" 0 "Error: invalid expected count" \
  "2" "1" "replace:sample.txt:old:new:abc" "exit"

# R-04: /fetch text must print the result, not discard it.
probe "R-04 fetch text prints errors" 0 "Error fetching URL" \
  "2" "1" "/fetch http://127.0.0.1:9 text" "exit"
probe "R-04 fetch text prints content" 0 "lorem ipsum" \
  "2" "1" "/fetch http://127.0.0.1:$SERVE_PORT/big.txt text" "exit"

# R-05: /fetch of large content without prior chat must not crash.
probe "R-05 fetch large content without chat survives" 0 "AI service unavailable" \
  "2" "1" "/fetch http://127.0.0.1:$SERVE_PORT/big.txt" "exit"
probe "R-05 fetch large json without chat survives" 0 "AI service unavailable" \
  "2" "1" "/fetch http://127.0.0.1:$SERVE_PORT/big.json json" "exit"

# R-06: path policy — traversal and symlink escapes refused, valid reads kept.
echo "outside secret" > ../probe_secret.txt
# ln -s needs privileges on some platforms (e.g. Windows without
# Developer Mode): only run symlink probes when the link is created.
if ln -s ../probe_secret.txt escape_link.txt 2>/dev/null; then
  HAVE_SYMLINK=1
else
  HAVE_SYMLINK=0
fi
# Restricted absolute path per OS (Windows has no /proc; an MSYS-style
# /proc path would resolve inside the root there).
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*) RESTRICTED_ABS="C:/Windows/System32/drivers/etc/hosts" ;;
  *) RESTRICTED_ABS="/proc/self/status" ;;
esac
probe "R-06 read traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "read:../probe_secret.txt" "exit"
if [ "$HAVE_SYMLINK" -eq 1 ]; then
  probe "R-06 read symlink escape refused" 0 "Path escapes the permitted root" \
    "2" "1" "read:escape_link.txt" "exit"
fi
probe "R-06 read restricted absolute refused" 0 "Restricted system path" \
  "2" "1" "read:$RESTRICTED_ABS" "exit"
probe "R-06 read absolute inside root works" 0 "beta" \
  "2" "1" "read:$WORK/sample.txt:1:1" "exit"
probe "R-06 write traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "write:../probe_evil.txt hi" "exit"
# Hermetic: drop any leftover from earlier runs sharing the temp parent.
rm -f ../probe_evil.txt
printf '%s\n' "2" "1" "write:../probe_evil.txt hi" "exit" | "$BIN" > /dev/null 2>&1
if [ -f ../probe_evil.txt ]; then
  FAIL=$((FAIL + 1))
  echo "FAIL: R-06 refused write still created the file"
else
  PASS=$((PASS + 1))
  echo "PASS: R-06 refused write created no file"
fi
probe "R-06 write inside root works" 0 "written successfully" \
  "2" "1" "write:created.txt hello" "exit"
probe "R-06 replace traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "replace:../probe_secret.txt:old:new" "exit"

# R-06 extended: @-injection, grep:, and directory readers share the policy.
# Injection content lands in memory via the processed chat input, so these
# probes read it back through read:data/memory.txt.
probe_absent "R-06 injection traversal leaks no content" 0 "outside secret" \
  "2" "1" "@../probe_secret.txt hi" "read:data/memory.txt" "exit"
probe "R-06 injection traversal refused" 0 "Access denied" \
  "2" "1" "@../probe_secret.txt hi" "read:data/memory.txt" "exit"
if [ "$HAVE_SYMLINK" -eq 1 ]; then
  probe_absent "R-06 injection symlink leaks no content" 0 "outside secret" \
    "2" "1" "@escape_link.txt hi" "read:data/memory.txt" "exit"
fi
probe "R-06 injection inside root still works" 0 "alpha" \
  "2" "1" "@sample.txt hi" "read:data/memory.txt" "exit"
probe "R-06 grep traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "grep:secret:.." "exit"
probe "R-06 grep inside root still works" 0 "alpha" \
  "2" "1" "grep:alpha:." "exit"
probe "R-06 todos traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "todos:.." "exit"
probe "R-06 analyze traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "analyze:.." "exit"
probe "R-06 tree traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "tree:.." "exit"
probe "R-06 components traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "components:.." "exit"
probe "R-06 files traversal escape refused" 0 "Path escapes the permitted root" \
  "2" "1" "/files .." "exit"
probe "R-06 files inside root still works" 0 "alpha" \
  "2" "1" "/files sample.txt" "exit"

# R-07: MCP lifecycle wording must disclose simulation.
probe "R-07 mcp start discloses simulation" 0 "MCP transport is not implemented" \
  "2" "1" "/mcp start filesystem" "exit"
probe "R-07 mcp stop discloses simulation" 0 "MCP transport is not implemented" \
  "2" "1" "/mcp stop filesystem" "exit"
probe "R-07 mcp status unknown server honest" 0 "Not registered" \
  "2" "1" "/mcp status __no_such_server__" "exit"
probe "R-07 mcp status registered server stopped" 0 "Stopped" \
  "2" "1" "/mcp status filesystem" "exit"

# R-08: /auth must not claim live connections from key presence.
probe "R-08 auth test without key honest" 0 "No key configured" \
  "2" "1" "/auth test together" "exit"
probe "R-08 auth test with key honest" 0 "not verified" \
  "2" "1" "/auth key together fakekey123" "/auth test together" "exit"
probe "R-08 auth status with key honest" 0 "Key present (not verified)" \
  "2" "1" "/auth key together fakekey123" "/auth status together" "exit"

# R-09: search must not demand or mention SERPAPI_KEY.
probe_absent "R-09 search needs no SerpAPI key" 0 "SERPAPI_KEY" \
  "2" "1" "search:hello world" "exit"

# R-10: /stats must not report unmeasured token usage.
probe_absent "R-10 stats reports no phantom token usage" 0 "Token Usage" \
  "2" "1" "/stats" "exit"
probe "R-10 stats still reports commands" 0 "Commands Processed" \
  "2" "1" "/stats" "exit"

# github:threads usage errors need no network and must not crash.
probe "threads usage on bad spec" 0 "Usage: github:threads:owner/repo:pr" \
  "2" "1" "github:threads:bad" "exit"
probe "threads usage on non-numeric pr" 0 "Usage: github:threads:owner/repo:pr" \
  "2" "1" "github:threads:o/r:abc" "exit"
probe "threads usage on missing pr" 0 "Usage: github:threads:owner/repo:pr" \
  "2" "1" "github:threads:o/r" "exit"
probe "threads help lists subcommand" 0 "github:threads:o/r:pr" \
  "2" "1" "/tools" "exit"

# Consistency matrix: recognition (accepted, not Unknown), dispatch to the
# intended handler, and controlled failure. All deterministic, no network:
# network-backed commands use invalid-arg or missing-input variants that
# stop before any request. Execution of live capabilities is covered by
# the focused probes above, not re-proven here.
probe "MX unknown direct command" 0 "Unknown command" \
  "2" "1" "frobnicate:xyz" "exit"
probe "MX unknown meta command" 0 "Unknown meta command" \
  "2" "1" "/frobnicate" "exit"
probe "MX meta help lists" 0 "Available meta commands" \
  "2" "1" "/help" "exit"
probe "PX main help uses chat session syntax" 0 "/chat save|resume <tag>" \
  "2" "1" "help" "exit"
probe_absent "PX main help omits stale session command" 0 "/save <name>" \
  "2" "1" "help" "exit"
probe_absent "PX main help does not promise shell toggle" 0 "Toggle shell" \
  "2" "1" "help" "exit"
probe "PX main help uses direct GitHub syntax" 0 "github:threads:<owner>/<repo>:<pr>" \
  "2" "1" "help" "exit"
probe "PX meta help uses direct GitHub syntax" 0 "github:repo:owner/repo" \
  "2" "1" "/help" "exit"
probe_absent "PX meta help omits slash GitHub syntax" 0 "/github repo:" \
  "2" "1" "/help" "exit"
probe "MX tools list" 0 "Available tools" \
  "2" "1" "/tools" "exit"
probe "MX search empty query errors" 0 "Error:" \
  "2" "1" "search:" "exit"
probe "MX write bare usage" 0 "Usage: write:filename content" \
  "2" "1" "write:" "exit"
probe "MX replace short usage" 0 "Usage: replace:" \
  "2" "1" "replace:onlyfile" "exit"
probe "MX read missing file errors" 0 "File does not exist" \
  "2" "1" "read:/nonexistent_xyz_123" "exit"
probe "MX fetch bare usage" 0 "Usage: /fetch" \
  "2" "1" "/fetch" "exit"
probe_absent "MX bare verbs reach handlers" 0 "Unknown meta command" \
  "2" "1" "/mcp" "exit"
probe_absent "MX bare theme reaches handler" 0 "Unknown meta command" \
  "2" "1" "/theme" "exit"
probe_absent "MX bare auth reaches handler" 0 "Unknown meta command" \
  "2" "1" "/auth" "exit"
probe_absent "MX bare sandbox reaches handler" 0 "Unknown meta command" \
  "2" "1" "/sandbox" "exit"
probe_absent "MX bare error reaches handler" 0 "Unknown meta command" \
  "2" "1" "/error" "exit"
probe_absent "MX bare checkpoint reaches handler" 0 "Unknown meta command" \
  "2" "1" "/checkpoint" "exit"
probe_absent "MX bare files reaches handler" 0 "Unknown meta command" \
  "2" "1" "/files" "exit"
probe_absent "MX bare chat reaches handler" 0 "Unknown meta command" \
  "2" "1" "/chat" "exit"
probe_absent "MX bare restore reaches handler" 0 "Unknown meta command" \
  "2" "1" "/restore" "exit"
probe_absent "MX bare context reaches handler" 0 "Unknown meta command" \
  "2" "1" "/context" "exit"
probe "MX fetch bad url errors" 0 "Invalid URL" \
  "2" "1" "/fetch notaurl" "exit"
probe "MX injection missing file marked" 0 "File not found" \
  "2" "1" "@missing_xyz_123 hi" "read:data/memory.txt" "exit"
probe "MX checkpoint list empty" 0 "No checkpoints found" \
  "2" "1" "/checkpoint list" "exit"
probe "MX mcp servers listed" 0 "filesystem" \
  "2" "1" "/mcp servers" "exit"
probe "MX theme list" 0 "Available themes" \
  "2" "1" "/theme list" "exit"
probe "MX auth providers listed" 0 "Available authentication providers" \
  "2" "1" "/auth providers" "exit"
probe "MX error recent empty" 0 "No recent errors" \
  "2" "1" "/error recent" "exit"
probe "MX context show empty" 0 "No hierarchical context found" \
  "2" "1" "/context show" "exit"
probe "MX github bad spec usage" 0 "Usage: github:repo:owner/repo" \
  "2" "1" "github:repo:bad" "exit"
probe "MX bare bang usage" 0 "Usage: !<command>" \
  "2" "1" "!" "exit"

# Shell routing with no docker on PATH: deny by default, explicit opt-in.
probe "routing denies without docker" 0 "Sandbox unavailable" \
  "2" "1" "!echo hi" "exit"
probe "routing opt-out cancels" 0 "Command cancelled by user" \
  "2" "1" "!echo hi" "N" "exit"
probe "routing opt-in runs on host" 0 "hi" \
  "2" "1" "!echo hi" "y" "exit"
probe "routing cmd denies without docker" 0 "Sandbox unavailable" \
  "2" "1" "cmd:echo hi" "exit"
probe "routing bare bang is usage" 0 "Usage: !<command>" \
  "2" "1" "!" "exit"

# Fake docker for routing success/failure paths (no daemon required).
mkdir -p fakebin
cat > fakebin/docker <<'EOF'
#!/bin/bash
if [ "$1" = "--version" ]; then
  echo "Docker version 99.0-fake, build test"
  exit 0
fi
if [ "$1" = "stop" ]; then
  exit 0
fi
case "${FAKE_DOCKER_MODE:-ok}" in
  fail)
    echo "fake: cannot connect to the Docker daemon" >&2
    exit 1
    ;;
  *)
    echo "fake-output"
    exit 0
    ;;
esac
EOF
chmod +x fakebin/docker
PROBE_PATH="$WORK/fakebin:$BASE_PATH"

probe "routing sandboxes with docker" 0 "Sandboxed (docker" \
  "2" "1" "!echo hi" "exit"
probe "routing sandbox output shown" 0 "fake-output" \
  "2" "1" "!echo hi" "exit"
export FAKE_DOCKER_MODE=fail
probe "routing docker failure reported" 0 "Exit code: 1" \
  "2" "1" "!echo hi" "exit"
export FAKE_DOCKER_MODE=ok

echo "----"
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
