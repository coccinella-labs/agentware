# UI Completeness and State Consistency Report

**Target:** Agentware v0.2.0 (`aa6c72ee`)
**Audit Dimension:** User interface fidelity, state consistency, feedback loops, and error transparency
**Verification Method:** REPL inspection, command parsing tracing, output text verification

---

## 1. UI Discoverability and Documentation Consistency

Agentware maintains three separate help mechanisms in the presentation layer:
1. `Utils::UI::print_help()` (invoked via `help` at the main prompt).
2. `Agent::show_meta_help()` (invoked via `/help` or `/?`).
3. `Agent::show_available_tools()` (invoked via `/tools`).

### Comparative Audit of Help Systems

| Capability / Command | Actual Parser Syntax | `UI::print_help()` (`help`) | `Agent::show_meta_help()` (`/help`) | `Agent::show_available_tools()` (`/tools`) | Discrepancy Status |
|---|---|---|---|---|---|
| **Save Chat** | `/chat save <tag>` | `/save <name> [tags]` ❌ | `/chat save <tag>` ✔️ | Not listed | Parser rejects `print_help` form |
| **Resume Chat** | `/chat resume <tag>` | `/resume <name>` ❌ | `/chat resume <tag>` ✔️ | Not listed | Parser rejects `print_help` form |
| **List Chats** | `/chat list` | `/sessions` ❌ | `/chat list` ✔️ | Not listed | Parser rejects `print_help` form |
| **Create Checkpoint** | `/checkpoint create <name>` | `/checkpoint <name>` ❌ | `/checkpoint <cmd>` ✔️ | Not listed | `print_help` omits required action verb |
| **List Checkpoints** | `/checkpoint list` | `/checkpoints` ❌ | Listed under `restore` ✔️ | Not listed | Parser rejects `/checkpoints` |
| **GitHub Operations** | `github:repo:owner/repo` | Not listed ❌ | `/github repo:owner/repo` ❌ | `github:repo:owner/repo` ✔️ | `/help` claims `/github` slash syntax; parser rejects it |
| **Review Threads** | `github:threads:o/r:pr` | Not listed ❌ | Not listed ❌ | `github:threads:o/r:pr[q]` ✔️ | Undocumented in main help menus |
| **Shell Invocation** | `!<command>` | `!` - Toggle shell ❌ | `!<command>` ✔️ | `cmd:command` ✔️ | `print_help` claims shell toggle; actually requires prefixed command |
| **File Injection** | `@<path>` | `@file <path>` ❌ | `@<path>` ✔️ | Not listed | `print_help` introduces phantom `@file` keyword |
| **Directory Injection** | `@<path>` | `@directory <path>` ❌ | `@<path>` ✔️ | Not listed | `print_help` introduces phantom `@directory` keyword |
| **Directory Tree** | `tree:[path]` | Not listed ❌ | Not listed ❌ | `tree:[path]` ✔️ | Omitted from primary help |
| **Component Analysis** | `components:[path]` | Not listed ❌ | Not listed ❌ | `components:[path]` ✔️ | Omitted from primary help |
| **Task / TODO Scan** | `todos:[path]` | Not listed ❌ | Not listed ❌ | `todos:[path]` ✔️ | Omitted from primary help |
| **Structure Analysis** | `analyze:[path]` | Not listed ❌ | Not listed ❌ | `analyze:[path]` ✔️ | Omitted from primary help |

---

## 2. State Consistency & Honest Reporting

### 1. Ready Banner vs Runtime Reality
- **Rendered Output (`src/ui.cpp:284-289`):**
  ```text
  System: Mode=Online | Model=Together AI | Memory=Active | Commands=Available
  ```
- **Audit Findings:**
  - `Memory=Active`: Always prints "Active", even if the `data/` directory is unwritable or permissions fail.
  - `Commands=Available`: Hardcoded static string; does not test process execution capabilities or external tool availability.

### 2. Double Ready Prompt Output
- **Rendered Output (`src/ui.cpp:297-300`):**
  ```text
  Ready - Type a command:
  Ready - Type a command or chat naturally:
  ```
- **Audit Findings:**
  - Duplicate print statements back-to-back in `print_ready_interface()`. Unpolished presentation artifact.

### 3. Session Statistics vs Configured Provider
- **Command:** `/stats` (`src/agent.cpp:1272-1280`)
- **Code Logic:**
  ```cpp
  std::string mode_str =
      (mode_ == Agent::Mode::MODE_TOGETHER   ? "Together AI"
       : mode_ == Agent::Mode::MODE_CEREBRAS ? "Cerebras"
                                             : "Local Ollama");
  ```
- **State Inconsistency:**
  - If the agent is configured to use Fireworks, Groq, DeepSeek, or OpenAI, `/stats` reports `Mode: Local Ollama`!
  - Does not reflect the user's actual online cloud provider selection.

### 4. Enterprise Status Check Simulation
- **Function:** `Utils::UI::print_enterprise_status()` (`src/ui.cpp:175-207`)
- **Rendered Output:**
  ```text
  Status
  ----------------------------------------
  Files (4/4)
     File injection
     Multi-file operations
     Context hierarchy
     Shell integration

  Sessions (4/4)
     Session management
     Tool registry
     Configuration
     Context compression

  Extensions (4/4)
     MCP servers
     Checkpoints
     Web fetch
     File filtering

  Security (4/4)
     Themes
     Authentication
     Sandboxing
     Error handling

  16 features active
  ----------------------------------------
  ```
- **State Inconsistency:**
  - Contains zero dynamic checks; all 16 items are hardcoded strings.
  - Furthermore, the function is never called anywhere in the agent lifecycle.

---

## 3. Feedback and Failure Reporting Transparency

| Scenario | UI Message Given | Actual System State | Honest / Degraded / Misleading |
|---|---|---|---|
| **Empty File Read (`read:empty.txt`)** | *Nothing printed (blank line)* | File exists and contains 0 bytes | **Confusing / Degraded** (User cannot distinguish empty file from silent failure) |
| **Invalid Regex in Grep (`grep:[a-`)** | `No matches found for pattern: [a-` | Regex compilation failed | **Misleading** (Reports zero matches instead of syntax error) |
| **MCP Server Start (`/mcp start fs`)** | `MCP server 'fs' start requested (simulated...)` | In-memory flag toggled; no OS process spawned | **Honest** (Explicitly warns user of simulation) |
| **Auth Key Verification (`/auth test key`)** | `Key present for: together (not verified...)` | File contains encrypted key; no network probe done | **Honest** (Explicitly warns user that live test was skipped) |
| **Shell Command (No Docker)** | `Sandbox unavailable (Docker not found)... Continue? (y/N)` | Host execution requires manual confirmation | **Honest & Safe** (Denies by default on EOF/N) |
| **Error Diagnostics (`/error report`)** | `Error Report: Total Errors: 0` | Errors occurred during session, but `log_error` was not called | **Misleading** (Implies clean execution despite operational failures) |
