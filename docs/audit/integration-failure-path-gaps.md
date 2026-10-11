# Integration and Failure-Path Coverage Gaps

**Target:** Agentware v0.2.0 (`aa6c72ee`)  
**Audit Dimension:** Automated test efficacy, failure scenario coverage, cross-layer contract verification  
**Test Suite Inventory:**
- GoogleTest Unit Tests: `build/agentware-tests` (64/64 passed)
- CTest Integration Harness: `ctest --test-dir build` (2/2 passed)
- Scripted Remediation Probes: `src/tests/remediation_probes.sh` (88/88 passed)
- Shell & Docker E2E Runner: `src/tests/run_e2e_tests.sh` / `docker-compose.e2e.yml`

---

## 1. What the Existing Test Suite Proves

The automated test suite verifies several core contracts:
1. **Security & Containment:** Canonical path traversal containment is tested thoroughly across `read:`, `write:`, `replace:`, `@` injection, `grep:`, `todos:`, `tree:`, `components:`, and `/files` (including symlink escapes and restricted absolute paths).
2. **Sandbox Argv Assembly:** Proves that Docker commands are constructed strictly as argv vectors without invoking a host shell (`build_docker_argv`).
3. **Session Fault Tolerance:** Verifies that persistence failures (`MemoryPersistenceBoundary`), missing optional tools, and API errors in `github:issues` / `github:health` do not crash the interactive REPL.
4. **Shell Routing:** Proves that missing Docker falls back to explicit host opt-in confirmation with deny-by-default on cancellation/EOF.
5. **String & Number Parsing:** Proves strict integer parser rejects non-numeric and mixed tokens.

---

## 2. Identified Test and Integration Gaps

Despite 100% test pass rates on existing suites, the product-level audit identified critical integration and failure-path coverage gaps:

### Gap 1: Zero Automated E2E Verification of Help Menus vs Command Parser
- **Defect Uncaught by Tests:** `UI::print_help()` advertises `/save`, `/resume`, `/sessions`, `/checkpoints`, `@file`, `@directory`, and `Toggle shell`—none of which work in the command parser.
- **Root Cause:** No unit or integration test asserts that the strings output by `UI::print_help()` or `Agent::show_meta_help()` correspond to valid dispatch grammar in `Agent::process_user_input()`.
- **Remediation Requirement:** A parser consistency test that iterates every command token mentioned in help text and verifies that passing it to `process_user_input` routes to a valid handler instead of `Unknown meta command` or syntax error.

### Gap 2: Zero Integration Tests for `AuthService` with `AIService`
- **Defect Uncaught by Tests:** Tests verify that `AuthService` can encrypt and decrypt keys in `auth_config.json`, but no test checks whether configuring a key through `/auth key <provider> <key>` enables `AIService` to use it.
- **Root Cause:** Unit tests exercise `AuthService` in isolation. The integration boundary between `AuthService` and `Agent::initialize_mode()` / `AIService` was never implemented or tested.

### Gap 3: Zero Integration Tests for `ThemeService` with `Utils::UI`
- **Defect Uncaught by Tests:** Unit tests verify that `ThemeService::set_theme()` updates internal maps and writes JSON, but no test verifies whether `Utils::UI::print_logo()` or `print_ready_interface()` changes ANSI output colors when the theme is changed.

### Gap 4: Missing Live Endpoints / Fallback Testing in CI
- **Defect Uncaught by Tests:** CI runs with offline mock endpoints or unit stubs. The behavior of real external network timeouts (e.g. CPR curl socket timeouts, DNS resolution delays, partial HTTP chunks) under real network drop conditions is not covered by automated pipelines.

### Gap 5: In-Memory Error Log Pipeline Disconnected
- **Defect Uncaught by Tests:** Tests for `ErrorService` verify that adding `ErrorInfo` objects manually can be exported or printed. However, zero tests verify that system failures (such as failed file writes, invalid JSON responses, or command execution timeouts) actually invoke `ErrorService::log_error`.

---

## 3. Failure-Path Matrix: Tested vs Untested Scenarios

| Failure Scenario | Component Boundary | Handled in Code? | Automated Test Exists? | Risk Assessment |
|---|---|---|---|---|
| **Path Traversal Escape** | L1 → L2 → L3 | Yes (`[Access denied]`) | Yes (`remediation_probes.sh`) | Low (Well-covered) |
| **Empty Replace Pattern** | L2 → L3 | Yes (`Error: Pattern cannot be empty`) | Yes (`remediation_probes.sh`) | Low (Well-covered) |
| **Components Nonexistent Dir** | L2 → L3 | Yes (catches `filesystem_error`) | Yes (`remediation_probes.sh`) | Low (Well-covered) |
| **GitHub REST 404 / 401 / 500** | L3 → L4 | Yes (catches in handlers) | Yes (`main_test.cpp`) | Low (Well-covered) |
| **Missing Docker Daemon** | L2 → L3 → L4 | Yes (prompts opt-in, denies on N) | Yes (`remediation_probes.sh`) | Low (Well-covered) |
| **Memory File Unwritable** | L2 → L3 → L4 | Yes (warns, session continues) | Yes (`main_test.cpp`) | Low (Well-covered) |
| **Unadvertised Help Commands** | L1 → L2 | No (returns `Unknown command`) | **NO** | Medium (UX Failure) |
| **Auth Key Stored but Unread**| L2 → L3 | No (chat fails for lack of env var) | **NO** | High (Functional Breakage) |
| **DNS / Web Search Socket Hang**| L3 → L4 | Partial (libcurl timeout applies) | **NO** | Medium (Degraded UX) |
| **Regex Syntax Exception in Grep**| L3 | Partial (swallowed to "No matches") | **NO** | Medium (Misleading Status) |
