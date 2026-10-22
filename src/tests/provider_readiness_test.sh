#!/usr/bin/env bash
# Regression tests for the provider readiness wait the golden suites use.
#
# Two things are pinned here, because both have already gone wrong once:
#
#   1. A healthy mock provider must be ready PROMPTLY. The suite used to
#      allow a provider up to 200 iterations, which on a loaded runner was
#      35 s per case: the provider was healthy, the wait was expensive, and
#      a provider that missed the cap was reported as never having come up.
#      The delay had a cause outside the wait itself (the server resolved
#      its own bind address with a reverse DNS lookup before writing the
#      port file), so the fix is pinned directly as well: the server must
#      not perform that lookup.
#
#   2. A provider that genuinely fails to start must fail FAST and say why.
#      A dead process, a port file that nothing listens on, and a provider
#      that stays silent are three different faults, and the wait and its
#      diagnostics must distinguish them instead of spending the deadline
#      and printing one sentence.
#
# POSIX-only, like the golden suites: it needs bash, perl and /dev/tcp.
#
# Usage: provider_readiness_test.sh
# Exits 0 when every case passes, 1 otherwise.
set -u

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MOCK_PROVIDER="${MOCK_PROVIDER:-${TESTS_DIR}/mock_tool_provider.py}"
# shellcheck source=provider_readiness.sh
. "${TESTS_DIR}/provider_readiness.sh"

TMP="$(mktemp -d)"
STARTED_PIDS=""
CASES=0
FAILURES=0

stop_started() {
  local pid
  for pid in $STARTED_PIDS; do
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  STARTED_PIDS=""
}

on_exit() {
  local status=$?
  stop_started
  rm -rf "$TMP"
  exit "$status"
}
trap on_exit EXIT

pass() {
  echo "PASS  $1"
}

fail() {
  echo "FAIL  $1 ($2)"
  FAILURES=$((FAILURES + 1))
}

# Starts the real mock provider and remembers its PID for cleanup.
start_mock() {
  local dir="$1" replies="${2:-}"
  [ -n "$replies" ] || replies='["ready"]'
  mkdir -p "$dir"
  printf '%s\n' "$replies" > "$dir/replies.json"
  MOCK_RESPONSES="$dir/replies.json" \
  MOCK_PROVIDER_PORT_FILE="$dir/port" \
  MOCK_PROVIDER_PID_FILE="$dir/pids" \
    python3 "$MOCK_PROVIDER" >"$dir/log" 2>&1 &
  STARTED_PIDS="$STARTED_PIDS $!"
  MOCK_PID=$!
}

# Starts a stub that stands in for a provider that misbehaves. The stub is
# given one line of shell to run.
start_stub() {
  local dir="$1" body="$2"
  mkdir -p "$dir"
  printf '#!/usr/bin/env bash\n%s\n' "$body" > "$dir/provider.sh"
  chmod +x "$dir/provider.sh"
  "$dir/provider.sh" >"$dir/log" 2>&1 &
  STARTED_PIDS="$STARTED_PIDS $!"
  STUB_PID=$!
}

echo "== provider readiness =="

# ---------------------------------------------------------------------------
# 1. A healthy provider is ready promptly, and the port it publishes is the
#    port that answers.
# ---------------------------------------------------------------------------
CASES=$((CASES + 1))
start_mock "$TMP/healthy"
if wait_for_provider "$MOCK_PID" "$TMP/healthy/port" 10; then
  published=$(tr -cd '0-9' < "$TMP/healthy/port")
  if [ "$WAIT_PORT" != "$published" ]; then
    fail "healthy-provider-ready-promptly" "waited on port $WAIT_PORT, port file says $published"
  elif [ "$WAIT_ELAPSED_MS" -ge 5000 ]; then
    fail "healthy-provider-ready-promptly" "ready after ${WAIT_ELAPSED_MS}ms, which is not prompt"
  else
    pass "healthy-provider-ready-promptly (${WAIT_ELAPSED_MS}ms)"
  fi
else
  fail "healthy-provider-ready-promptly" "the healthy provider was not ready: $WAIT_REASON"
fi
stop_started

# ---------------------------------------------------------------------------
# 2. The server does not resolve its own bind address. http.server does that
#    with socket.getfqdn(), a reverse DNS lookup that is pointless for a
#    loopback fixture and blocks for the resolver's timeout when the resolver
#    does not answer. A host with a fast resolver cannot show the difference,
#    so the lookup itself is asserted against rather than its cost.
# ---------------------------------------------------------------------------
CASES=$((CASES + 1))
bound_name=$(python3 - "$MOCK_PROVIDER" <<'PY'
import importlib.util
import sys

spec = importlib.util.spec_from_file_location("mock_provider", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
server = module.Server(("127.0.0.1", 0), module.Handler)
try:
    print(server.server_name)
finally:
    server.server_close()
PY
)
if [ "$bound_name" = "127.0.0.1" ]; then
  pass "server-bind-does-not-resolve-the-address"
else
  fail "server-bind-does-not-resolve-the-address" \
    "server_name is '$bound_name', so the bind resolved the address (getfqdn)"
fi

# ---------------------------------------------------------------------------
# 3. A provider that dies is reported as dead, immediately.
# ---------------------------------------------------------------------------
CASES=$((CASES + 1))
start_stub "$TMP/exits" "exit 3"
if wait_for_provider "$STUB_PID" "$TMP/exits/port" 10; then
  fail "provider-that-exits-fails-fast" "a provider that exited was reported ready"
else
  if [ "$WAIT_ELAPSED_MS" -ge 3000 ]; then
    fail "provider-that-exits-fails-fast" "took ${WAIT_ELAPSED_MS}ms to notice an exit"
  elif ! printf '%s' "$WAIT_REASON" | grep -q "exited"; then
    fail "provider-that-exits-fails-fast" "reason does not mention the exit: $WAIT_REASON"
  else
    pass "provider-that-exits-fails-fast (${WAIT_ELAPSED_MS}ms)"
  fi
fi
stop_started

# ---------------------------------------------------------------------------
# 4. A provider that stays alive but says nothing is cut off by the deadline,
#    which is wall clock: one second means one second, not 200 iterations.
# ---------------------------------------------------------------------------
CASES=$((CASES + 1))
start_stub "$TMP/silent" "sleep 30"
if wait_for_provider "$STUB_PID" "$TMP/silent/port" 1; then
  fail "silent-provider-hits-the-deadline" "a silent provider was reported ready"
else
  if [ "$WAIT_ELAPSED_MS" -lt 950 ] || [ "$WAIT_ELAPSED_MS" -ge 3000 ]; then
    fail "silent-provider-hits-the-deadline" \
      "a 1s deadline took ${WAIT_ELAPSED_MS}ms, so it is not wall clock"
  elif ! printf '%s' "$WAIT_REASON" | grep -q "no port file"; then
    fail "silent-provider-hits-the-deadline" "reason does not name the missing port file: $WAIT_REASON"
  else
    pass "silent-provider-hits-the-deadline (${WAIT_ELAPSED_MS}ms)"
  fi
fi
# The diagnostic must say what was missing, not just that something was.
diag=$(describe_provider_failure "$STUB_PID" "$TMP/silent/port" "$TMP/silent/log")
if printf '%s' "$diag" | grep -q "was never written"; then
  pass "diagnostic-names-the-missing-port-file"
else
  CASES=$((CASES + 1))
  fail "diagnostic-names-the-missing-port-file" "the diagnostic did not name the port file state"
fi
stop_started

# ---------------------------------------------------------------------------
# 5. A port file whose port accepts nothing is its own fault, not "never
#    came up": the provider wrote a port, and the wait must say so.
# ---------------------------------------------------------------------------
CASES=$((CASES + 1))
# The stub needs the port file path in its environment, so it is started
# here rather than through start_stub.
mkdir -p "$TMP/no-listener"
printf '%s\n' '#!/usr/bin/env bash' \
  'printf "1\n" > "$MOCK_PROVIDER_PORT_FILE"' \
  'sleep 30' > "$TMP/no-listener/provider.sh"
chmod +x "$TMP/no-listener/provider.sh"
MOCK_PROVIDER_PORT_FILE="$TMP/no-listener/port" \
  "$TMP/no-listener/provider.sh" >"$TMP/no-listener/log" 2>&1 &
STARTED_PIDS="$STARTED_PIDS $!"
no_listener_pid=$!
if wait_for_provider "$no_listener_pid" "$TMP/no-listener/port" 2; then
  fail "unanswered-port-file-is-reported-as-such" "port 1 was reported as accepting connections"
else
  if ! printf '%s' "$WAIT_REASON" | grep -q "nothing accepted a connection"; then
    fail "unanswered-port-file-is-reported-as-such" "reason does not mention the connect: $WAIT_REASON"
  else
    pass "unanswered-port-file-is-reported-as-such (${WAIT_ELAPSED_MS}ms)"
  fi
fi
stop_started

# ---------------------------------------------------------------------------
# 6. Binding is unchanged: a port that is already taken is still an error,
#    reported by the provider itself, and it still exits non-zero.
# ---------------------------------------------------------------------------
CASES=$((CASES + 1))
mkdir -p "$TMP/occupied"
python3 - "$TMP/occupied/port" <<'PY' >"$TMP/occupied/port" 2>/dev/null &
import socket
import sys
import time

sock = socket.socket()
sock.bind(("127.0.0.1", 0))
sock.listen(1)
print(sock.getsockname()[1], flush=True)
time.sleep(30)
PY
occupier=$!
STARTED_PIDS="$STARTED_PIDS $occupier"
for _ in $(seq 1 100); do
  [ -s "$TMP/occupied/port" ] && break
  sleep 0.05
done
taken=$(tr -cd '0-9' < "$TMP/occupied/port")
if [ -z "$taken" ]; then
  fail "bind-error-is-preserved" "the occupier did not publish a port"
else
  MOCK_RESPONSES="$TMP/occupied/replies.json" \
  MOCK_PROVIDER_PORT_FILE="$TMP/occupied/mock.port" \
    python3 "$MOCK_PROVIDER" "$taken" >"$TMP/occupied/mock.log" 2>&1
  status=$?
  if [ "$status" -eq 0 ]; then
    fail "bind-error-is-preserved" "the mock exited 0 although port $taken was taken"
  elif ! grep -qiE "address already in use|10048" "$TMP/occupied/mock.log"; then
    fail "bind-error-is-preserved" "the bind error is not reported in the log"
  else
    pass "bind-error-is-preserved"
  fi
fi
stop_started

if [ "$FAILURES" -eq 0 ]; then
  echo
  echo "$CASES/$CASES provider readiness cases passed"
  exit 0
fi
echo
echo "$FAILURES of $CASES provider readiness cases failed"
exit 1
