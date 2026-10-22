#!/usr/bin/env bash
# Shared readiness wait for the mock providers the golden suites start.
#
# The tool-call golden harness used to wait for a provider with a fixed
# number of iterations (`for i in $(seq 1 200)`). That is not a wall-clock
# bound. On a loaded runner each iteration costs up to a fifth of a second,
# so the cap alone was a 35 s delay per case, and a provider that was up but
# late was reported as "never accepted a connection" with nothing to act on.
#
# This wait has a deadline in seconds, records why it gave up, and leaves
# killing the process to the caller, which owns the PID. It is sourced by
# run_tool_loop_golden.sh and by provider_readiness_test.sh, so the harness
# and its regression test exercise the same code.
#
# Results, set on every call:
#   WAIT_PORT        the port, on success; empty on failure
#   WAIT_REASON      what went wrong, on failure
#   WAIT_ELAPSED_MS  how long the wait took, in milliseconds
#
# Requires: bash, perl (for a sub-second clock).

# Milliseconds since the epoch. perl is used instead of date because macOS
# date has no %N and the harness already depends on perl for its clock.
provider_now_ms() {
  perl -MTime::HiRes -e 'printf "%.0f", Time::HiRes::time() * 1000'
}

# Waits until the provider publishes a port in <port-file> and that port
# accepts a connection.
#
# usage: wait_for_provider <pid> <port-file> [deadline-seconds]
#
# The deadline is wall clock, not an iteration count: SECONDS is a bash
# builtin, so checking it costs no process. A provider that exits is
# reported immediately instead of being waited out, and the three ways this
# can fail (no port file, a port that nothing listens on, a dead process)
# are distinguished, because they have different causes.
# The deadline is wall clock, not an iteration count, and it is read in
# milliseconds: a whole-second counter would fire up to a second early or
# late, which is the same class of bug as counting iterations. Reading the
# clock costs one fork, and only on iterations that did not already
# succeed, so a healthy provider pays nothing for it.
wait_for_provider() {
  local pid="$1" port_file="$2" limit="${3:-10}"
  local start_ms deadline_ms port="" seen_port=""
  start_ms=$(provider_now_ms)
  deadline_ms=$((start_ms + limit * 1000))
  WAIT_PORT=""
  WAIT_REASON=""
  WAIT_ELAPSED_MS=0
  while :; do
    if [ -s "$port_file" ]; then
      port=$(tr -cd '0-9' < "$port_file" 2>/dev/null)
      if [ -n "$port" ]; then
        seen_port="$port"
        if (exec 3<>/dev/tcp/127.0.0.1/"$port") 2>/dev/null; then
          WAIT_PORT="$port"
          WAIT_ELAPSED_MS=$(( $(provider_now_ms) - start_ms ))
          return 0
        fi
      fi
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      WAIT_ELAPSED_MS=$(( $(provider_now_ms) - start_ms ))
      if [ -n "$seen_port" ]; then
        WAIT_REASON="the provider exited after publishing port ${seen_port}, which never accepted a connection"
      else
        WAIT_REASON="the provider exited before it published a port"
      fi
      return 1
    fi
    if [ "$(provider_now_ms)" -ge "$deadline_ms" ]; then
      WAIT_ELAPSED_MS=$(( $(provider_now_ms) - start_ms ))
      if [ -n "$seen_port" ]; then
        WAIT_REASON="the provider published port ${seen_port} in ${port_file} but nothing accepted a connection within ${limit}s"
      elif [ -s "$port_file" ]; then
        WAIT_REASON="the provider wrote ${port_file} without a usable port number and did not fix it within ${limit}s"
      else
        WAIT_REASON="the provider published no port file at ${port_file} within ${limit}s"
      fi
      return 1
    fi
    sleep 0.05
  done
}

# Prints, indented for a failing case, why the wait failed and the state
# that would explain it. A bare "never accepted a connection" was once all a
# reader got, and it could mean a dead process, a slow start, a wrong port,
# or a resolver stall; these lines separate them.
#
# usage: describe_provider_failure <pid> <port-file> <log-file>
describe_provider_failure() {
  local pid="$1" port_file="$2" log_file="$3"
  echo "      | waited ${WAIT_ELAPSED_MS}ms: ${WAIT_REASON}"
  if kill -0 "$pid" 2>/dev/null; then
    echo "      | provider pid ${pid} is still alive"
  else
    echo "      | provider pid ${pid} is gone"
  fi
  if [ -s "$port_file" ]; then
    echo "      | port file ${port_file} contains: $(tr -d '\n' < "$port_file" 2>/dev/null)"
  elif [ -e "$port_file" ]; then
    echo "      | port file ${port_file} exists but is empty"
  else
    echo "      | port file ${port_file} was never written"
  fi
  if [ -r "$log_file" ]; then
    echo "      | --- provider log (last 20 lines) ---"
    tail -20 "$log_file" 2>/dev/null | sed 's/^/      | /'
  else
    echo "      | provider log ${log_file} was never written"
  fi
}
