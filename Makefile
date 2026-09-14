# Agentware Agent Makefile
# Professional AI Agent with Command Execution

.PHONY: all build clean test test-e2e test-e2e-docker package install docker-build preflight preflight-quick preflight-full preflight-ci full-check setup install-deps-mac install-deps-ubuntu lint-yaml lint-all help

# Default target
all: build

# Build the project
build:
	@echo "Building Agentware Agent..."
	@mkdir -p build
	@cd build && cmake .. && make

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf build
	@rm -rf dist
	@echo "Clean complete."

# Run basic tests
test: build
	@echo "Running basic tests..."
	@printf "2\\nexit\\n" | ./build/bin/agentware-agent

# Run E2E tests locally
test-e2e: build
	@echo "Running E2E tests locally..."
	@./src/tests/run_e2e_tests.sh

# Run E2E tests in Docker
test-e2e-docker:
	@echo "Running E2E tests in Docker..."
	@docker-compose -f docker-compose.e2e.yml up --build --abort-on-container-exit

# Create distribution package
package: build
	@echo "Creating distribution package..."
	@mkdir -p dist
	@cp build/bin/agentware-agent dist/
	@cp README.md dist/
	@cp LICENSE dist/
	@cp .env.example dist/
	@echo "Package created in dist/"

# Install locally
install: build
	@echo "Installing Agentware Agent locally..."
	@sudo cp build/bin/agentware-agent /usr/local/bin/
	@echo "Installation complete. Run 'agentware-agent' to start."

# Build Docker image
docker-build:
	@echo "Building Docker image..."
	@docker build -t agentware/agent -f Dockerfile .

# Run preflight checks (smart detection)
preflight:
	@echo "Running preflight checks..."
	@./src/scripts/preflight.sh

# Run quick preflight checks
preflight-quick:
	@echo "Running quick preflight checks..."
	@QUICK=true ./src/scripts/preflight.sh

# Run full preflight checks
preflight-full:
	@echo "Running full preflight checks..."
	@QUICK=false ./src/scripts/preflight.sh

# Run CI-style preflight checks
preflight-ci:
	@echo "Running CI-style preflight checks..."
	@CI=true ./src/scripts/preflight.sh

# Run full check (clean, build, test, preflight-quick, preflight)
full-check:
	@echo "Running full check..."
	@./src/scripts/full-check.sh

# Run CI locally (simulate GitHub Actions)
ci:
	@echo "Running local CI simulation..."
	@make clean && make build && make test && make preflight-ci

# Setup development environment
setup:
	@echo "Setting up development environment..."
	@./src/scripts/setup-git-hooks.sh

# Install dependencies on macOS
install-deps-mac:
	@echo "Installing dependencies on macOS..."
	@brew install cpr nlohmann-json cmake

# Install dependencies on Ubuntu
install-deps-ubuntu:
	@echo "Installing dependencies on Ubuntu..."
	@sudo apt update
	@sudo apt install -y nlohmann-json3-dev cmake build-essential git libcurl4-openssl-dev
	@rm -rf /tmp/cpr
	@git clone --depth=1 https://github.com/libcpr/cpr.git /tmp/cpr
	@cmake -S /tmp/cpr -B /tmp/cpr/build -DBUILD_CPR_TESTS=OFF -DCPR_USE_SYSTEM_CURL=ON -DCMAKE_BUILD_TYPE=Release
	@sudo cmake --build /tmp/cpr/build --target install
	@sudo ldconfig
	@rm -rf /tmp/cpr

# Lint YAML files
lint-yaml:
	@echo "Running yamllint on YAML files..."
	@yamllint -c .yamllint.yml .

# Run pre-commit on all files
lint-all:
	@echo "Running pre-commit on all files..."
	@pre-commit run --all-files

# Run clang-tidy on source files
clang-tidy: build
	@echo "Running clang-tidy on source files..."
	@if [ -f "build/compile_commands.json" ]; then \
		CLANG_TIDY_EXTRA_ARGS="--extra-arg=-isystem/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1 --extra-arg=-isystem/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include --system-headers=0"; \
		find src -name "*.cpp" -exec clang-tidy {} -p build $$CLANG_TIDY_EXTRA_ARGS \; ; \
	else \
		echo "Error: compile_commands.json not found. Run cmake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"; \
	fi

# Show help
help:
	@echo "Agentware Agent - Available Make Targets:"
	@echo ""
	@echo "Build Commands:"
	@echo "  build              Build the project"
	@echo "  clean              Clean build artifacts"
	@echo "  test               Run basic tests"
	@echo "  test-e2e           Run E2E tests locally"
	@echo "  test-e2e-docker    Run E2E tests in Docker"
	@echo "  package            Create distribution package"
	@echo ""
	@echo "Code Quality:"
	@echo "  lint-yaml          Run yamllint on YAML files"
	@echo "  lint-all           Run all pre-commit hooks"
	@echo "  clang-tidy         Run clang-tidy static analysis"
	@echo ""
	@echo "Installation:"
	@echo "  install            Install locally to /usr/local/bin"
	@echo "  install-deps-mac   Install dependencies on macOS"
	@echo "  install-deps-ubuntu Install dependencies on Ubuntu"
	@echo ""
	@echo "Development:"
	@echo "  setup              Setup development environment"
	@echo "  preflight          Run preflight checks (smart)"
	@echo "  preflight-quick    Run quick preflight checks"
	@echo "  preflight-full     Run full preflight checks"
	@echo "  preflight-ci       Run CI-style preflight checks"
	@echo "  full-check         Run full check (clean, build, test, preflights)"
	@echo ""
	@echo "Docker:"
	@echo "  docker-build       Build Docker image"
	@echo ""
	@echo "Other:"
	@echo "  help               Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make build         # Build the project"
	@echo "  make test-e2e      # Run E2E tests"
	@echo "  make preflight     # Run preflight checks"
	@echo "  make package       # Create distribution"
	@echo "  make install       # Install locally"
