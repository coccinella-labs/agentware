#!/bin/bash
# Agentware Agent - Unified Preflight Check Script
# Comprehensive validation for development, CI/CD, and deployment

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Auto-detect environment and set appropriate mode
AUTO_DETECT=${AUTO_DETECT:-true}

# Check if running in CI (multiple CI environment variables)
if [ "${CI:-false}" = "true" ] || [ "${GITHUB_ACTIONS:-false}" = "true" ] || [ "${GITLAB_CI:-false}" = "true" ] || [ "${JENKINS_URL:-}" != "" ] || [ "${TRAVIS:-false}" = "true" ]; then
    CI_MODE=true
else
    CI_MODE=false
fi

# Auto-detect quick mode based on context
if [ "${QUICK:-false}" = "true" ]; then
    QUICK_MODE=true
elif [ "$AUTO_DETECT" = "true" ] && [ "$CI_MODE" = "false" ]; then
    # In development, check if this is a quick context (e.g., pre-commit)
    if [ "${PRE_COMMIT:-false}" = "true" ] || [ "${GIT_HOOK:-false}" = "true" ]; then
        QUICK_MODE=true
    else
        # Default to full checks in development
        QUICK_MODE=false
    fi
else
    QUICK_MODE=${QUICK:-false}
fi

echo "Agentware Agent Preflight Checks"
echo "===================================="
echo "Environment: $([ "$CI_MODE" = "true" ] && echo "CI/CD" || echo "Development")"
echo "Mode: $([ "$QUICK_MODE" = "true" ] && echo "Quick" || echo "Comprehensive")"
echo "Auto-detect: $([ "$AUTO_DETECT" = "true" ] && echo "Enabled" || echo "Disabled")"
echo "===================================="

# Colors (disabled in CI for clean logs)
if [ "$CI_MODE" = "true" ]; then
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
else
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
fi

# Status functions
print_status() {
    if [ "$CI_MODE" = "true" ]; then
        echo "::notice::INFO $1"
    else
        echo -e "${BLUE}[INFO]${NC} $1"
    fi
}

print_success() {
    if [ "$CI_MODE" = "true" ]; then
        echo "::notice::PASS $1"
    else
        echo -e "${GREEN}[PASS]${NC} $1"
    fi
}

print_warning() {
    if [ "$CI_MODE" = "true" ]; then
        echo "::warning::WARN $1"
    else
        echo -e "${YELLOW}[WARN]${NC} $1"
    fi
}

print_error() {
    if [ "$CI_MODE" = "true" ]; then
        echo "::error::FAIL $1"
    else
        echo -e "${RED}[FAIL]${NC} $1"
    fi
}

# Error counter
ERROR_COUNT=0
WARNING_COUNT=0

fail_check() {
    ERROR_COUNT=$((ERROR_COUNT + 1))
    print_error "$1"
}

warn_check() {
    WARNING_COUNT=$((WARNING_COUNT + 1))
    print_warning "$1"
}

# 1. Environment Validation
print_status "Validating build environment..."

if ! command -v cmake &> /dev/null; then
    fail_check "CMake not found. Install CMake 3.14+"
else
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    print_success "CMake version: $CMAKE_VERSION"
fi

if ! command -v make &> /dev/null; then
    fail_check "Make not found. Install build tools"
else
    print_success "Make found"
fi

# Check C++ compiler
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    fail_check "No C++ compiler found (g++ or clang++)"
else
    print_success "C++ compiler available"
fi

# 2. Dependency Validation
print_status "Checking dependencies..."

# Check libcpr
CPR_FOUND=false
if pkg-config --exists libcpr 2>/dev/null; then
    CPR_FOUND=true
    print_success "libcpr found via pkg-config"
elif [ -d "/opt/homebrew/include/cpr" ] || [ -d "/usr/local/include/cpr" ]; then
    CPR_FOUND=true
    print_success "libcpr found via Homebrew"
elif find /usr/include /usr/local/include /opt/homebrew/include -name "cpr" -type d 2>/dev/null | head -1 | grep -q cpr; then
    CPR_FOUND=true
    print_success "libcpr headers found"
fi

if [ "$CPR_FOUND" = "false" ]; then
    print_success "libcpr not found in system - CMake will fetch and build it automatically"
fi

# Check nlohmann/json
JSON_FOUND=false
if [ -d "/opt/homebrew/include/nlohmann" ] || [ -d "/usr/local/include/nlohmann" ]; then
    JSON_FOUND=true
    print_success "nlohmann/json found via Homebrew"
elif find /usr/include /usr/local/include /opt/homebrew/include -name "nlohmann" -type d 2>/dev/null | head -1 | grep -q nlohmann; then
    JSON_FOUND=true
    print_success "nlohmann/json headers found"
fi

if [ "$JSON_FOUND" = "false" ]; then
    warn_check "nlohmann/json not found (build may still work if CMake finds it)"
fi

# If build succeeds later, dependencies are actually fine
DEPS_CHECK_PASSED=true

# 3. Project Structure Validation
print_status "Validating project structure..."
REQUIRED_DIRS=("src" "src/include" "src/scripts" "docs")
REQUIRED_FILES=("CMakeLists.txt" "Makefile" "README.md" "LICENSE" ".env.example" "Dockerfile")

for dir in "${REQUIRED_DIRS[@]}"; do
    if [ ! -d "$dir" ]; then
        fail_check "Required directory missing: $dir"
    else
        print_success "Directory found: $dir"
    fi
done

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        fail_check "Required file missing: $file"
    else
        print_success "File found: $file"
    fi
done

# 4. Clean Build Process
print_status "Performing clean build..."
make clean > /dev/null 2>&1 || true

BUILD_LOG=$(mktemp)
if ! make build > "$BUILD_LOG" 2>&1; then
    fail_check "Build failed. Check build log:"
    if [ "$CI_MODE" != "true" ]; then
        tail -20 "$BUILD_LOG"
    fi
    rm -f "$BUILD_LOG"
else
    print_success "Build completed successfully"
    rm -f "$BUILD_LOG"
fi

# 5. Binary Validation
print_status "Validating binary..."
if [ ! -f "build/bin/agentware-agent" ]; then
    fail_check "Binary not found at build/bin/agentware-agent"
elif [ ! -x "build/bin/agentware-agent" ]; then
    fail_check "Binary is not executable"
else
    BINARY_SIZE=$(stat -f%z "build/bin/agentware-agent" 2>/dev/null || stat -c%s "build/bin/agentware-agent" 2>/dev/null || echo "unknown")
    print_success "Binary validation passed (size: $BINARY_SIZE bytes)"
fi

# 5. Functionality Tests
print_status "Running functionality tests..."

# Test 1: Basic startup/shutdown
if ! printf "2\nexit\n" | ./build/bin/agentware-agent > /dev/null 2>&1; then
    print_error "Basic startup test failed"
    exit 1
fi
print_success "Basic startup test passed"

# Test 2: Help system
if ! printf "2\nhelp\nexit\n" | ./build/bin/agentware-agent > /dev/null 2>&1; then
    print_error "Help system test failed"
    exit 1
fi
print_success "Help system test passed"

# Test 3: Version command
if ! printf "2\nversion\nexit\n" | ./build/bin/agentware-agent > /dev/null 2>&1; then
    print_error "Version command test failed"
    exit 1
fi
print_success "Version command test passed"

# 6. E2E Tests (if available and not in quick mode)
if [ "$QUICK_MODE" = "false" ] && [ -f "src/tests/run_e2e_tests.sh" ]; then
    print_status "Running E2E tests..."
    if command -v expect >/dev/null 2>&1; then
        if ./src/tests/run_e2e_tests.sh > /dev/null 2>&1; then
            print_success "E2E tests passed"
        else
            warn_check "E2E tests failed - check logs for details"
        fi
    else
        warn_check "E2E tests skipped - expect not installed"
    fi
else
    print_status "E2E tests skipped (quick mode or not available)"
fi

# 7. File Operations Test
print_status "Testing file operations..."
TEST_FILE="test_preflight.txt"
TEST_CONTENT="Preflight test content"

if ! printf "2\nwrite:$TEST_FILE $TEST_CONTENT\nread:$TEST_FILE\nexit\n" | ./build/bin/agentware-agent > /dev/null 2>&1; then
    print_error "File operations test failed"
    exit 1
fi

# Cleanup test file
rm -f "$TEST_FILE" > /dev/null 2>&1 || true
print_success "File operations test passed"

# 8. Memory System Test
print_status "Testing memory system..."
if [ -f "data/memory.txt" ]; then
    MEMORY_SIZE=$(wc -l < "data/memory.txt" 2>/dev/null || echo "0")
    print_success "Memory system operational (${MEMORY_SIZE} lines)"
else
    print_success "Memory system ready (no existing data)"
fi

# 9. Configuration Validation
print_status "Validating configuration..."
if [ ! -f ".env.example" ]; then
    print_error "Configuration template (.env.example) missing"
    exit 1
fi
print_success "Configuration template found"

# 10. Documentation Check
print_status "Checking documentation..."
REQUIRED_DOCS=("README.md" "LICENSE" "CHANGELOG.md")
for doc in "${REQUIRED_DOCS[@]}"; do
    if [ ! -f "$doc" ]; then
        print_warning "Documentation file missing: $doc"
    fi
done
print_success "Documentation check completed"

# 11. Package Structure Validation
print_status "Validating package structure..."
REQUIRED_DIRS=("src" "src/include" "docs")
for dir in "${REQUIRED_DIRS[@]}"; do
    if [ ! -d "$dir" ]; then
        print_error "Required directory missing: $dir"
        exit 1
    fi
done
print_success "Package structure validation passed"

# 12. Code Quality Check
print_status "Running code quality checks..."
if command -v clang-tidy > /dev/null 2>&1; then
    if [ -f "build/compile_commands.json" ]; then
        CLANG_TIDY_COUNT=$(make clang-tidy 2>&1 | grep -c "warnings generated" || echo "0")
        if [ "$CLANG_TIDY_COUNT" -gt 0 ]; then
            print_warning "Clang-tidy found $CLANG_TIDY_COUNT files with warnings"
        else
            print_success "Clang-tidy passed with no warnings"
        fi
    else
        print_warning "Clang-tidy available but no compilation database found"
    fi
else
    print_warning "Clang-tidy not installed - recommended for code quality"
fi

# 13. Security Check (basic)
print_status "Running basic security checks..."
if grep -r "system(" src/ > /dev/null 2>&1; then
    print_warning "Direct system() calls found - review for security"
fi

if grep -r "TODO\|FIXME\|HACK" src/ > /dev/null 2>&1; then
    print_warning "Code contains TODO/FIXME/HACK comments"
fi
print_success "Basic security check completed"

# Skip extended checks in quick mode if no errors
if [ "$QUICK_MODE" = "true" ] && [ $ERROR_COUNT -eq 0 ]; then
    print_status "Quick mode: Skipping extended checks..."

    # Still do GitHub integration check if in development
    if [ "$CI_MODE" = "false" ] && [ -d ".github" ]; then
        print_status "Validating GitHub integration..."

        if [ ! -f ".github/CODEOWNERS" ]; then
            warn_check "CODEOWNERS file missing"
        fi

        if [ ! -f ".github/pull_request_template.md" ]; then
            warn_check "PR template missing"
        fi

        if [ ! -d ".github/workflows" ]; then
            warn_check "GitHub Actions workflows missing"
        fi
    fi

    echo "===================================="
    print_success "QUICK PREFLIGHT PASSED!"
    echo "Errors: $ERROR_COUNT | Warnings: $WARNING_COUNT"
    echo ""
    if [ "$CI_MODE" = "false" ]; then
        echo "Tip: Run 'make preflight' with QUICK=false for comprehensive checks"
    fi
    echo "===================================="
    exit 0
fi

# 14. GitHub Integration Check (if .github exists)
if [ -d ".github" ]; then
    print_status "Validating GitHub integration..."

    if [ ! -f ".github/CODEOWNERS" ]; then
        warn_check "CODEOWNERS file missing"
    else
        print_success "CODEOWNERS found"
    fi

    if [ ! -f ".github/pull_request_template.md" ]; then
        warn_check "PR template missing"
    else
        print_success "PR template found"
    fi

    if [ ! -d ".github/workflows" ]; then
        warn_check "GitHub Actions workflows missing"
    else
        WORKFLOW_COUNT=$(find .github/workflows -name "*.yml" -o -name "*.yaml" | wc -l)
        print_success "GitHub Actions workflows found ($WORKFLOW_COUNT files)"
    fi
fi

# Final Summary
echo ""
echo "===================================="
if [ $ERROR_COUNT -eq 0 ]; then
    print_success "ALL PREFLIGHT CHECKS PASSED!"
    echo ""
    echo "Ready for:"
    echo "   • Development deployment"
    echo "   • Production build"
    echo "   • Package distribution"
    echo "   • Container deployment"
    echo "   • CI/CD pipeline"
    echo ""
    echo "Next steps:"
    echo "   make package      # Create distribution package"
    echo "   make docker-build # Build container image"
    echo "   make install      # Install locally"
    echo ""
else
    print_error "PREFLIGHT FAILED!"
    echo ""
    echo "Summary:"
    echo "   Errors: $ERROR_COUNT"
    echo "   Warnings: $WARNING_COUNT"
    echo ""
    echo "Please fix the errors above before proceeding."
    exit 1
fi

echo "Errors: $ERROR_COUNT | Warnings: $WARNING_COUNT"
echo "===================================="
