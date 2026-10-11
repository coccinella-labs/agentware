# Dependency Wiring and Reachability Report

**Target:** Agentware v0.2.0 (`aa6c72ee`)  
**Audit Scope:** Codebase-wide symbol reachability, dependency injection, and orphaned/dead paths  
**Verification Method:** Static call graph analysis, source code grepping, linker configuration review  

---

## 1. Executive Summary

While the primary interactive agent loop (`Core::Agent`) and its direct file, git, search, and container-sandboxing paths are tightly connected, several peripheral services exhibit significant wiring gaps:
- **Orphaned Domains:** `DatabaseService` is completely decoupled from the product.
- **Unwired Observability:** `ErrorService::log_error` has zero callers across the application.
- **Disconnected Config Stores:** `AuthService` stores and encrypts API keys, but the active agent chat loop (`AIService`) never reads from it.
- **Static vs Dynamic UI Disconnect:** `ThemeService` provides dynamic ANSI color themes, but `Utils::UI` renders the entire user interface using hardcoded color constants.

---

## 2. Unregistered & Orphaned Services

### 1. `agentware::DatabaseService` (Completely Orphaned)
- **Files:** [`src/database_service.cpp`](file:///Users/bniladridas/agentware/src/database_service.cpp), [`src/include/database_service.h`](file:///Users/bniladridas/agentware/src/include/database_service.h)
- **Status:** **Zero Callers**.
- **Evidence:**
  - `grep -rn "DatabaseService" src/` returns only its own definition and header.
  - Lives in a divergent namespace (`agentware::` instead of `Services::`).
  - Contains full connection logic (`pqxx::connection`), query execution, and parameter replacement.
  - Neither `main.cpp` nor `agent.cpp` includes its header or instantiates an object.
- **Impact:** Misleading codebase capability; compiled if `libpqxx` is present, but impossible for any user or agent command to access.

### 2. `Services::ErrorService::log_error` (Unwired Log Pipeline)
- **Files:** [`src/error_service.cpp:108`](file:///Users/bniladridas/agentware/src/error_service.cpp#L108), [`src/include/error_service.h`](file:///Users/bniladridas/agentware/src/include/error_service.h)
- **Status:** **Zero Callers**.
- **Evidence:**
  - `grep -rn "log_error" src/` returns only its declaration and implementation in `error_service.cpp`.
  - While `/error report` and `/error recent` are fully wired to `ErrorService`, the log itself is never populated during runtime errors in `AIService`, `WebService`, `FileService`, or `CommandService`.
- **Impact:** The advertised error diagnosis system is permanently empty at runtime.

### 3. `Utils::UI::print_enterprise_status` (Dead UI Function)
- **Files:** [`src/ui.cpp:175-207`](file:///Users/bniladridas/agentware/src/ui.cpp#L175-L207)
- **Status:** **Zero Callers**.
- **Evidence:**
  - Declared in `ui.h`, implemented in `ui.cpp`.
  - Outputs a hardcoded "16 features active" checklist across Files, Sessions, Extensions, and Security.
  - No call sites exist in `main.cpp`, `agent.cpp`, or tests.
- **Impact:** Phantom feature marketing code with zero runtime connection.

---

## 3. Disconnected Configuration & Data Flow

### 1. `AuthService` vs `AIService` Credential Disconnect
- **Current Architecture:**
  - `Agent::initialize_mode()` reads provider keys directly from OS environment variables (`TOGETHER_API_KEY`, `CEREBRAS_API_KEY`, etc.) via `Utils::Config::get_env_var()`.
  - `AuthService` maintains an encrypted JSON credential database (`data/auth_config.json`) via `/auth key <provider> <key>`.
  - `AIService` takes `api_key_` injected into its constructor at agent startup.
- **Wiring Gap:**
  - `AIService` never calls `AuthService::get_api_key()`.
  - If a user configures an API key using the visible `/auth key` command, that key is saved to `data/auth_config.json`, but the chat agent **never uses it**. If the matching environment variable is unset, the agent will refuse to start or report service unavailable.

### 2. `ThemeService` vs `Utils::UI` Theming Disconnect
- **Current Architecture:**
  - `Utils::UI` uses hardcoded escape sequences in `Utils::Color` (`Color::PINK`, `Color::GREEN`, `Color::RED`, etc.).
  - `ThemeService` provides themes (`default`, `dark`, `cyberpunk`, `ocean`) and saves preferences in `data/theme_config.json`.
- **Wiring Gap:**
  - `Agent::run()` and `Utils::UI` never query `ThemeService::colorize_*()` when rendering banners, spinners, dividers, or prompts.
  - Running `/theme set cyberpunk` alters the theme configuration and changes colors only inside theme preview commands, but does not affect the main REPL interface.

---

## 4. Dead Configuration & Environment Variables

| Variable / Flag | Documented Location | Code Reality | Status |
|---|---|---|---|
| `DB_HOST` | `.env.example` | Zero references in code | **Dead / Unused** |
| `DB_PORT` | `.env.example` | Zero references in code | **Dead / Unused** |
| `DB_NAME` | `.env.example` | Zero references in code | **Dead / Unused** |
| `DB_USER` | `.env.example` | Zero references in code | **Dead / Unused** |
| `DB_PASSWORD` | `.env.example` | Zero references in code | **Dead / Unused** |
| `SERPAPI_KEY` | `.env.example`, older docs | Read once in `web_service.cpp`, but DuckDuckGo instant search is used; never passed in HTTP requests | **Unused / Reserved** |

---

## 5. Unreachable & Duplicate UI Entry Points

| User Entry Point | Claimed Target | Actual Routing / Status | Gap Classification |
|---|---|---|---|
| `/save <name>` | Save session state | `Unknown meta command` | **Unreachable Syntax** (Parser expects `/chat save <tag>`) |
| `/resume <name>` | Resume session state | `Unknown meta command` | **Unreachable Syntax** (Parser expects `/chat resume <tag>`) |
| `/sessions` | List session states | `Unknown meta command` | **Unreachable Syntax** (Parser expects `/chat list`) |
| `/checkpoint <name>` | Create checkpoint | `Error: Checkpoint name is required` (misses action verb) | **Unreachable Syntax** (Parser expects `/checkpoint create <name>`) |
| `/checkpoints` | List checkpoints | `Unknown meta command` | **Unreachable Syntax** (Parser expects `/checkpoint list`) |
| `/github repo:...` | GitHub repo stats | `Unknown meta command` | **Unreachable Syntax** (Parser expects `github:repo:...`) |
| `! (alone)` | "Toggle shell" | Prints usage `Usage: !<command>` | **Obsolete Help Text** (Toggle feature removed; direct prefix execution implemented) |
| `@file <path>` | Inject file | Interprets `file` as file path (attempts to read file named `file`) | **Obsolete Help Text** (Syntax is `@<path>`, not `@file <path>`) |
| `@directory <path>` | Inject directory | Interprets `directory` as file path | **Obsolete Help Text** (Syntax is `@<path>`, not `@directory <path>`) |
