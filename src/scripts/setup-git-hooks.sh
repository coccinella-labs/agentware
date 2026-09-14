#!/bin/bash
# Setup Git hooks for automatic preflight checks

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

echo "Setting up Git hooks for Agentware Agent..."

# Create .git/hooks directory if it doesn't exist
mkdir -p .git/hooks

# Pre-commit hook (quick checks)
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
# Pre-commit hook - Quick preflight checks

echo "Running pre-commit preflight checks..."

# Set environment variables for quick mode
export PRE_COMMIT=true
export GIT_HOOK=true

# Run quick preflight
if ! make preflight-quick; then
    echo ""
    echo "Pre-commit checks failed!"
    echo "Fix the issues above or use 'git commit --no-verify' to skip"
    exit 1
fi

echo "Pre-commit checks passed!"
EOF

# Pre-push hook (comprehensive checks)
cat > .git/hooks/pre-push << 'EOF'
#!/bin/bash
# Pre-push hook - Comprehensive preflight checks

echo "Running pre-push preflight checks..."

# Set environment variable for comprehensive mode
export GIT_HOOK=true

# Run full preflight
if ! make preflight; then
    echo ""
    echo "Pre-push checks failed!"
    echo "Fix the issues above or use 'git push --no-verify' to skip"
    exit 1
fi

echo "Pre-push checks passed!"
EOF

# Make hooks executable
chmod +x .git/hooks/pre-commit
chmod +x .git/hooks/pre-push

echo "Git hooks installed successfully!"
echo ""
echo "What happens now:"
echo "  • git commit  → Runs quick preflight checks"
echo "  • git push    → Runs comprehensive preflight checks"
echo ""
echo "To bypass hooks (use carefully):"
echo "  • git commit --no-verify"
echo "  • git push --no-verify"
echo ""
echo "To remove hooks:"
echo "  • rm .git/hooks/pre-commit .git/hooks/pre-push"
