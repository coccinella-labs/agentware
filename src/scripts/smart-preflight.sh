#!/bin/bash
# Smart Preflight - Automatically chooses the right preflight mode
# This is the ONE command that handles all scenarios

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Detect context and choose appropriate mode
detect_context() {
    # Check if in CI environment
    if [ "${CI:-false}" = "true" ] || [ "${GITHUB_ACTIONS:-false}" = "true" ] || [ "${GITLAB_CI:-false}" = "true" ] || [ "${JENKINS_URL:-}" != "" ]; then
        echo "ci"
        return
    fi

    # Check if in pre-commit hook
    if [ "${PRE_COMMIT:-false}" = "true" ] || [ "${GIT_HOOK:-false}" = "true" ]; then
        echo "quick"
        return
    fi

    # Check if user explicitly wants quick mode
    if [ "${QUICK:-false}" = "true" ]; then
        echo "quick"
        return
    fi

    # Check if this is a development build (has uncommitted changes)
    if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
        if ! git diff-index --quiet HEAD -- 2>/dev/null; then
            echo "dev-quick"
            return
        fi
    fi

    # Default to comprehensive for clean development environment
    echo "comprehensive"
}

CONTEXT=$(detect_context)

echo "Smart Preflight Detection"
echo "Context: $CONTEXT"
echo ""

# The check has three speeds now, and the old QUICK and CI environment
# variables no longer mean anything: the script takes NO_BUILD,
# PREFLIGHT_CLEAN and PREFLIGHT_CLANG_TIDY instead.
case $CONTEXT in
    "ci")
        # In CI the binary is built by the job, so the check does not
        # build again.
        echo "Running CI preflight..."
        NO_BUILD=1 exec ./src/scripts/preflight.sh
        ;;
    "quick"|"dev-quick")
        # Fast path: use the binary that is already there.
        echo "Running quick preflight..."
        NO_BUILD=1 exec ./src/scripts/preflight.sh
        ;;
    "comprehensive")
        echo "Running comprehensive preflight..."
        exec ./src/scripts/preflight.sh
        ;;
    *)
        echo "Unknown context, defaulting to comprehensive..."
        exec ./src/scripts/preflight.sh
        ;;
esac
