#!/bin/bash
# Agentware Agent - local preflight check
#
# This is a developer convenience tool, not a test suite and not a CI
# gate. It answers one question: "is this checkout in a state where the
# agent can be built and started here?"
#
# Behavior is covered by the suites that assert it:
#   * ctest -R basic_tests        (src/tests/main_test.cpp)
#   * src/tests/run_routing_golden.sh
#   * src/tests/run_tool_loop_golden.sh
#   * src/tests/remediation_probes.sh
# Nothing here duplicates those, and nothing here can stand in for them.
#
# Configuration (all optional):
#   NO_BUILD=1             skip the build step and use the existing binary
#   PREFLIGHT_CLEAN=1      build from scratch (default: incremental)
#   PREFLIGHT_CLANG_TIDY=1 run clang-tidy as well (slow, opt-in)
#
# Usage: src/scripts/preflight.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

NO_BUILD="${NO_BUILD:-0}"
CLEAN_BUILD="${PREFLIGHT_CLEAN:-0}"
RUN_CLANG_TIDY="${PREFLIGHT_CLANG_TIDY:-0}"

# Scratch files this script creates. They are removed by the EXIT trap,
# which also runs on an interrupt, so an aborted run leaves nothing
# behind.
BUILD_LOG=""
BUILD_PID=""
TEST_FILE="preflight_write_check.txt"
ERROR_COUNT=0
WARNING_COUNT=0
# Set to 1 only when the checks have run to the end. The EXIT trap uses
# it to tell "passed" apart from "interrupted".
COMPLETED=0

cleanup() {
    local status=$?
    if [ -n "$BUILD_PID" ] && kill -0 "$BUILD_PID" 2>/dev/null; then
        kill "$BUILD_PID" 2>/dev/null || true
        wait "$BUILD_PID" 2>/dev/null || true
    fi
    if [ -n "$BUILD_LOG" ]; then rm -f "$BUILD_LOG"; fi
    if [ -n "$TEST_FILE" ]; then rm -f "$TEST_FILE"; fi
    # A run that never reached the end was interrupted, not passed. Bash
    # does not run the INT trap when only this script is signalled while a
    # foreground child is running, so the exit status is decided here
    # instead: the checks are marked complete only on the way out.
    if [ "$COMPLETED" -eq 0 ] && [ "$status" -eq 0 ]; then
        status=130
    fi
    exit $status
}
# The EXIT trap cleans up on every exit path, including an interrupt.
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' BLUE='' NC=''
fi

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
warn() {
    WARNING_COUNT=$((WARNING_COUNT + 1))
    echo -e "${YELLOW}[WARN]${NC} $1"
}
fail() {
    ERROR_COUNT=$((ERROR_COUNT + 1))
    echo -e "${RED}[FAIL]${NC} $1"
}

info "Agentware Agent preflight (local use)"
info "======================================"
if [ "$NO_BUILD" = "1" ]; then
    info "Build: skipped (NO_BUILD=1), using the existing binary"
elif [ "$CLEAN_BUILD" = "1" ]; then
    info "Build: clean build requested (PREFLIGHT_CLEAN=1)"
else
    info "Build: incremental (set PREFLIGHT_CLEAN=1 for a clean build)"
fi
info "======================================"

# 1. Build tools. CMake and a compiler are the two things a checkout
#    cannot do without; make drives the targets.
info "Checking build tools..."
command -v cmake >/dev/null 2>&1 || { fail "cmake not found (install CMake 3.14+)"; }
command -v make >/dev/null 2>&1 || { fail "make not found (install the build tools)"; }
if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    fail "no C++ compiler found (g++ or clang++)"
fi
if [ "$ERROR_COUNT" -eq 0 ]; then
    pass "build tools present (cmake $(cmake --version | head -n1 | cut -d' ' -f3))"
fi

# 2. Build. The default is an incremental build: a clean build of this
#    project costs minutes and tells us nothing a failed incremental
#    build would not also tell us.
if [ "$NO_BUILD" = "1" ]; then
    info "Skipping the build."
elif [ "$ERROR_COUNT" -ne 0 ]; then
    info "Skipping the build because the build tools are missing."
else
    info "Building..."
    BUILD_LOG="$(mktemp)"
    if [ "$CLEAN_BUILD" = "1" ]; then
        make clean >/dev/null 2>&1 || true
    fi
    # The build runs as a background job that is waited for, rather than
    # in the foreground. Bash defers a signal to a foreground child until
    # that child exits, so a targeted interrupt would otherwise be
    # ignored until the build finished.
    make build >"$BUILD_LOG" 2>&1 &
    BUILD_PID=$!
    if wait "$BUILD_PID"; then
        BUILD_PID=""
        pass "build succeeded"
        rm -f "$BUILD_LOG"
        BUILD_LOG=""
    else
        fail "build failed, last lines of the build log:"
        tail -20 "$BUILD_LOG" | sed 's/^/    /'
    fi
fi

# 3. The binary. A missing or unrunnable binary is the one failure a
#    developer hits before anything else, so it is a hard error.
info "Checking the binary..."
BINARY="build/bin/agentware-agent"
if [ ! -f "$BINARY" ]; then
    fail "binary not found at $BINARY (build it first, or drop NO_BUILD=1)"
elif [ ! -x "$BINARY" ]; then
    fail "binary at $BINARY is not executable"
else
    SIZE=$(wc -c <"$BINARY" | tr -d ' ')
    pass "binary present ($SIZE bytes)"
fi

# 4. Startup smoke test. The agent asks two questions before it accepts
#    a command: the mode (1 online, 2 offline) and then the offline
#    model. A piped run has to answer both, otherwise the second answer
#    is consumed as a model choice and the real command is never read.
#    The assertions live in the CTest suite; this only proves the
#    binary starts and exits cleanly here.
if [ "$ERROR_COUNT" -eq 0 ] && [ -x "$BINARY" ]; then
    info "Checking that the agent starts and exits..."
    RAW_OUTPUT=$(printf '2\n1\nexit\n' | "$BINARY" 2>&1) && STATUS=0 || STATUS=$?
    OUTPUT=$(printf '%s' "$RAW_OUTPUT" | sed 's/\x1b\[[0-9;]*m//g')
    if [ "$STATUS" -ne 0 ]; then
        fail "the agent did not start cleanly (exit status $STATUS)"
    elif printf '%s' "$OUTPUT" | grep -qF "Ready - Type a command"; then
        pass "agent starts, reaches the command prompt and exits"
    else
        fail "the agent did not reach the command prompt"
    fi
fi

# 5. Write path. This checks that the working directory is writable and
#    that a file written here reads back with the same content. It runs
#    in a scratch file that the trap removes.
if [ "$ERROR_COUNT" -eq 0 ] && [ -x "$BINARY" ]; then
    info "Checking the write path..."
    rm -f "$TEST_FILE"
    printf '2\n1\nwrite:%s preflight probe\nexit\n' "$TEST_FILE" | \
        "$BINARY" >/dev/null 2>&1 || true
    if [ -f "$TEST_FILE" ] && grep -qF "preflight probe" "$TEST_FILE"; then
        pass "a file written here reads back with the same content"
    else
        fail "a file written here did not read back with the same content"
    fi
fi

# 6. clang-tidy, opt-in. It takes minutes over the whole tree, so it
#    only runs when it is asked for. The exit status of the target is
#    honoured: a target that failed to run is a failure, not a clean
#    run with zero findings.
if [ "$RUN_CLANG_TIDY" = "1" ]; then
    info "Running clang-tidy (this takes a few minutes)..."
    TIDY_LOG="$(mktemp)"
    if make clang-tidy >"$TIDY_LOG" 2>&1; then
        FINDINGS=$(grep -c "warnings generated" "$TIDY_LOG" || true)
        if [ "${FINDINGS:-0}" -gt 0 ]; then
            warn "clang-tidy reported findings in $FINDINGS file(s); see the log above"
        else
            pass "clang-tidy reported no findings"
        fi
        rm -f "$TIDY_LOG"
    else
        fail "clang-tidy failed to run; last lines of its output:"
        tail -20 "$TIDY_LOG" | sed 's/^/    /'
        rm -f "$TIDY_LOG"
    fi
else
    info "Skipping clang-tidy (set PREFLIGHT_CLANG_TIDY=1 to run it)"
fi

COMPLETED=1
echo ""
info "======================================"
if [ "$ERROR_COUNT" -eq 0 ]; then
    pass "preflight passed"
else
    fail "preflight failed"
fi
info "Errors: $ERROR_COUNT | Warnings: $WARNING_COUNT"
info "======================================"

[ "$ERROR_COUNT" -eq 0 ]
