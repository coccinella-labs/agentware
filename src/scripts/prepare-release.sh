#!/bin/bash
# Agentware Agent - Release Preparation Script
# Comprehensive pre-release validation and preparation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"
VERSION=$(cat VERSION | tr -d '\n')

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}Agentware v${VERSION} Release Prep${NC}"
}

print_step() {
    echo -e "${BLUE}>${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}!${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_header

# Check clean working directory
print_step "Checking git status..."
if [ -n "$(git status --porcelain)" ]; then
    print_error "Uncommitted changes found"
    git status --short
    exit 1
fi
print_success "Git status clean"

# Check branch
print_step "Checking branch..."
CURRENT_BRANCH=$(git branch --show-current)
if [ "$CURRENT_BRANCH" != "main" ] && [ "$CURRENT_BRANCH" != "master" ]; then
    print_warning "Not on main (on: $CURRENT_BRANCH)"
    read -p "Continue? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
else
    print_success "On $CURRENT_BRANCH"
fi

# Check version consistency
print_step "Checking versions..."
VERSION_HEADER=$(grep "AGENTWARE_VERSION_STRING" build/include/version.h | cut -d'"' -f2)
VERSION_RELEASE=$(cat VERSION | tr -d '\n')
if [ "$VERSION_HEADER" != "$VERSION" ] || [ "$VERSION_RELEASE" != "$VERSION" ]; then
    print_error "Version mismatch: header=$VERSION_HEADER, release=$VERSION_RELEASE, expected=$VERSION"
    exit 1
fi
print_success "Versions match: $VERSION"

# Run preflight checks
print_step "Running preflight..."
if ! CI=false QUICK=false ./src/scripts/preflight.sh > /dev/null 2>&1; then
    print_error "Preflight failed"
    exit 1
fi
print_success "Preflight passed"

# Test app modes
print_step "Testing app..."
if ! printf "2\n1\nversion\nexit\n" | timeout 10 ./build/bin/agentware-agent > /dev/null 2>&1; then
    print_error "Offline test failed"
    exit 1
fi
print_success "Offline test passed"

# Check docs
print_step "Checking docs..."
REQUIRED_DOCS=("README.md" "CHANGELOG.md" "CONTRIBUTING.md")
for doc in "${REQUIRED_DOCS[@]}"; do
    [ -f "$doc" ] || { print_error "Missing: $doc"; exit 1; }
done
print_success "Docs present"

# Check TODOs
print_step "Checking TODOs..."
if grep -r "TODO\|FIXME\|HACK" src/ --exclude-dir=build 2>/dev/null > /dev/null; then
    print_warning "TODOs found"
    read -p "Continue? (y/N): " -n 1 -r
    echo
    [[ $REPLY =~ ^[Yy]$ ]] || exit 1
else
    print_success "No TODOs"
fi

# Check workflows
print_step "Checking workflows..."
[ -f ".github/workflows/ci.yml" ] || { print_error "Missing CI workflow"; exit 1; }
grep -q "name: CI/CD Pipeline" .github/workflows/ci.yml || { print_error "CI malformed"; exit 1; }
print_success "Workflows OK"

# Check build
print_step "Checking build..."
make clean > /dev/null 2>&1 && make build > /dev/null 2>&1 || { print_error "Build failed"; exit 1; }
print_success "Build OK"

# Check tag
print_step "Checking tag..."
TAG_NAME="v${VERSION}"
git tag -l | grep -q "^${TAG_NAME}$" && { print_error "Tag exists"; exit 1; }
print_success "Tag available: $TAG_NAME"

# Final summary
echo
echo -e "${GREEN}Release prep complete${NC}"
echo "Version: $VERSION | Branch: $CURRENT_BRANCH | Tag: $TAG_NAME"
echo
echo "Next: git tag -a $TAG_NAME -m 'Release $TAG_NAME' && git push origin $TAG_NAME"

# Optional tag creation
read -p "Create tag now? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    git tag -a "$TAG_NAME" -m "Release $TAG_NAME" && git push origin "$TAG_NAME"
    print_success "Tag pushed"
else
    echo "Manual: git tag -a $TAG_NAME -m 'Release $TAG_NAME' && git push origin $TAG_NAME"
fi
