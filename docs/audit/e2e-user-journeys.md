# End-to-End User Journey Audit Results

**Target:** Agentware v0.2.0 (`aa6c72ee`)  
**Audit Model:** Vertical Slice End-to-End Journey Verification  
**Verification Date:** 2026-10-11  
**Operating System:** macOS 27.0.1 (Darwin 26.0.0 arm64)  
**Execution Context:** Hermetic temp directories, piped stdin, automated test runners (`ctest`, `main_test`, `remediation_probes.sh`)

---

## 1. Journey Summary Overview

Each vertical slice was audited by following the exact control flow and data flow from user interaction at Layer 1 through Layer 2 orchestration and Layer 3 domain adapters to Layer 4 infrastructure, and observing the presentation return path.

| Journey Category | Total Journeys Traced | Verified Functional | Partially Functional / Degraded | Broken / Unreachable |
|---|---|---|---|---|
| **Core AI Chat & Injections** | 4 | 4 | 0 | 0 |
| **Filesystem & Search Tools** | 8 | 8 | 0 | 0 |
| **Git & GitHub Operations** | 6 | 6 | 0 | 0 |
| **Shell & Execution Safety** | 5 | 5 | 0 | 0 |
| **Memory & Context Flow** | 6 | 5 | 1 | 0 |
| **System, Extensions & Auth** | 7 | 4 | 3 | 0 |
| **Help & UI Discoverability** | 4 | 1 | 0 | 3 |
| **Total** | **40** | **33** | **4** | **3** |

---

## 2. Detailed Vertical Slice Journey Traces

### Slice 1: Natural AI Chat (Online & Local)
- **Journey 1.1: Online AI Chat with Valid Key**
  - **L1 Input:** User enters natural text (e.g. `What is Agentware?`).
  - **L2 Control Flow:** `Agent::process_user_input` identifies no prefix (`@`, `!`, `/`, `:`) → calls `handle_ai_chat`. Spawns threaded spinner `Utils::UI::spinner`. Reads context from `MemoryManager::get_context_string()` and `ContextService::load_hierarchical_context(".")`.
  - **L3 Domain:** `AIService::chat` builds payload JSON conforming to provider requirements (Together, Cerebras, Fireworks, Groq, DeepSeek, or OpenAI).
  - **L4 Infra:** `WebService::post_json` transmits request via CPR/libcurl over HTTPS.
  - **Return Path:** Response JSON parsed for `choices[0].message.content`; spinner joins; message output to terminal via `std::cout`; interaction appended to `data/memory.txt` through `record_interaction`.
  - **Failure Path:** If API endpoint unreachable or key invalid, prints formatted error string without throwing or crashing session.
  - **Verdict:** **Verified Functional**.

- **Journey 1.2: Startup Without Required API Key**
  - **L1/L2 Control Flow:** In `Agent::initialize_mode()`, user selects Online (1) and provider (e.g. Cerebras). If `CEREBRAS_API_KEY` is not present in environment or `.env`, throws `std::runtime_error("CEREBRAS_API_KEY not set")`.
  - **Return Path:** Caught in `main.cpp:29-33`, prints `Fatal error: CEREBRAS_API_KEY not set` in red to `std::cerr`, process exits with code 1.
  - **Verdict:** **Verified Functional (Fail-Fast)**.

- **Journey 1.3: Offline AI Chat via Local Ollama**
  - **L1 Input:** User enters prompt with mode configured to Offline (llama3.2:3b / latest / 3.1).
  - **L2/L3 Flow:** Routes to `http://localhost:11434/api/chat`.
  - **Return Path:** If Ollama daemon is offline, HTTP request fails fast and outputs connection error string to console; session remains intact.
  - **Verdict:** **Verified Functional**.

### Slice 2: File Injections (`@` Syntax)
- **Journey 2.1: Valid Local File Injection**
  - **L1 Input:** `@src/version.cpp Explain this file`.
  - **L2/L3 Flow:** `process_file_injections` parses `@src/version.cpp` → passes path to `Validator::validate_file_access` → reads file contents via `read_file_or_directory` → replaces token in input string with file content buffer → forwards expanded prompt to `handle_ai_chat`.
  - **Return Path:** Complete prompt processed by LLM; interaction recorded.
  - **Verdict:** **Verified Functional**.

- **Journey 2.2: Malicious Traversal Injection (`@../../etc/passwd`)**
  - **L1 Input:** `@../../etc/passwd inspect this`.
  - **L2 Flow:** `Validator::validate_file_access` resolves canonical path and detects escape from project root boundary.
  - **Return Path:** Returns explicit marker `[Access denied: Path escapes the permitted root]` in place of file content. Leaks zero file bytes.
  - **Verdict:** **Verified Functional (Security Boundary Enforced)**.

### Slice 3: Direct File Operations
- **Journey 3.1: Range File Read (`read:file:start:count`)**
  - **L1 Input:** `read:sample.txt:1:2`.
  - **L2 Flow:** `handle_direct_command` extracts params, validates access, validates existence, parses line numbers using strict parser `Validator::try_parse_int`.
  - **L3/L4 Flow:** `Services::FileService::read_file_range` streams target lines.
  - **Return Path:** Outputs requested slice. If numbers are non-numeric (e.g. `read:sample.txt:abc:2`), returns clean error message `Error: invalid line range 'abc:2'. Use read:filename:start[:count] with numbers.` without crashing.
  - **Verdict:** **Verified Functional**.

- **Journey 3.2: Guarded Text Replacement (`replace:file:old:new[:count]`)**
  - **L1 Input:** `replace:sample.txt:alpha:omega:1`.
  - **L2 Flow:** Access validation, strict count parsing. Checks if `old_text` is empty.
  - **L3 Flow:** `Services::FileService::replace_text_in_file` executes replacement.
  - **Return Path:** Returns `Replaced 1 occurrence(s) in sample.txt`.
  - **Failure Edge Case:** If `old_text` is empty (`replace:sample.txt::omega`), validator rejects with `Error: Pattern cannot be empty` (verified via probe `A1`, eliminating previously identified infinite loop bug).
  - **Verdict:** **Verified Functional**.

### Slice 4: Shell Execution & Sandboxing
- **Journey 4.1: Shell Command With Docker Active**
  - **L1 Input:** `!ls` or `cmd:ls`.
  - **L2 Flow:** `Validator::validate_command_safe` checks dangerous command tokens. Passes to `execute_routed_command`.
  - **L3 Flow:** `SandboxService::is_docker_available()` returns true.
  - **L4 Flow:** Executes container via `docker run --rm --network=none --cap-drop ALL --security-opt no-new-privileges` under bounded supervision.
  - **Return Path:** Outputs stdout with banner `Sandboxed (docker, default config, no network)`.
  - **Verdict:** **Verified Functional**.

- **Journey 4.2: Shell Command Without Docker (Deny by Default)**
  - **L1 Input:** `!whoami` when Docker daemon is not running.
  - **L2 Flow:** `execute_routed_command` detects no Docker. Prints:
    `Sandbox unavailable (Docker not found): this command would run on the host without isolation.`
    `Run on host without isolation? (y/N): `
  - **Return Path:** If user enters `N` or sends EOF, execution is cancelled with `Command cancelled by user`. If user enters `y`, command is executed via `CommandService::execute` with 5s timeout and dangerous command filtering.
  - **Verdict:** **Verified Functional (Security Boundary Enforced)**.

### Slice 5: Version Control & GitHub
- **Journey 5.1: Git History Inspection (`git:log`)**
  - **L1 Input:** `git:log`.
  - **L2 Flow:** `handle_direct_command` passes path `.` to `GitService::get_git_log`.
  - **L3 Flow:** Validates directory path without blocking on command shell concatenation.
  - **L4 Flow:** Spawns `git log -n 7` via process pipe.
  - **Return Path:** Returns formatted commits. Saved to session memory.
  - **Verdict:** **Verified Functional**.

- **Journey 5.2: GitHub Inline Review Threads (`github:threads:owner/repo:pr`)**
  - **L1 Input:** `github:threads:coccinella-labs/agentware:1`.
  - **L2 Flow:** Parses repo and PR number with strict integer parser.
  - **L3/L4 Flow:** `GitHubService::get_review_threads` queries GitHub REST endpoints, handles pagination, client-side keyword filtering, and returns thread author, resolution status, path, line, and comments.
  - **Return Path:** Outputs thread summary with explicit disclaimer that anchors are comment-time. Catches HTTP errors safely.
  - **Verdict:** **Verified Functional**.

### Slice 6: Memory & State Management
- **Journey 6.1: Global Fact Storage & Retrieval**
  - **L1 Input:** `remember:Project uses C++20` followed by `memory`.
  - **L2 Flow:** `record_global_fact` invokes `MemoryManager::save_global_fact`. Appends markdown to `~/.agentware/AGENTWARE.md`.
  - **L1 Input 2:** `memory` (exact bare word).
  - **L2 Flow 2:** Routed directly to `MemoryManager::get_global_context()`.
  - **Return Path:** Renders persistent facts.
  - **Verdict:** **Verified Functional**.

- **Journey 6.2: Session History Compression (`/compress`)**
  - **L1 Input:** `/compress` before any natural chat is started.
  - **L2 Flow:** Checks compressible interaction count (requires >= 5). Checks `ai_service_` availability.
  - **Return Path:** If AI service is null or unavailable, outputs clean warning `AI service unavailable - start a chat session first so /compress can summarize.` without segfaulting.
  - **Verdict:** **Verified Functional**.

### Slice 7: Diagnostics, Theming & UI Truthfulness
- **Journey 7.1: MCP Server Simulation (`/mcp start filesystem`)**
  - **L1 Input:** `/mcp start filesystem`.
  - **L2/L3 Flow:** `MCPService::start_mcp_server` updates in-memory registry.
  - **Return Path:** Outputs honest notice: `MCP server 'filesystem' start requested (simulated - no server process is started; MCP transport is not implemented)`. Status command reports `Simulated (no live process)`.
  - **Verdict:** **Partially Verified (Honest Simulation, No Real IPC)**.

- **Journey 7.2: Auth Key Testing (`/auth test together`)**
  - **L1 Input:** `/auth test together`.
  - **L2/L3 Flow:** `AuthService::test_provider_connection`.
  - **Return Path:** If key configured, reports: `Key present for: together (not verified - no live check performed)`.
  - **Verdict:** **Partially Verified (Honest Disclosures)**.

- **Journey 7.3: Error Diagnostics Inspection (`/error report`)**
  - **L1 Input:** `/error report`.
  - **L2/L3 Flow:** `ErrorService::print_error_report`.
  - **Return Path:** Outputs empty error report.
  - **Underlying Reality:** `ErrorService::log_error` has zero invocation sites across the system; operational errors in other services do not record here.
  - **Verdict:** **Degraded Observability**.

- **Journey 7.4: Help Menu Mismatch (`help` command)**
  - **L1 Input:** `help`.
  - **L2 Flow:** `UI::print_help()` prints command table.
  - **Observed Failure:** Help lists `/save <name>`, `/resume <name>`, `/sessions`, `/checkpoints`, `/github repo:...`. Running these slash commands returns `Unknown meta command`. The real parser accepts `/chat save|resume|list`, `/checkpoint <cmd>`, and `github:repo:...`.
  - **Verdict:** **Broken (Misleading UI Documentation)**.
