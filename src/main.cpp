#include <cstdlib>
#include <iostream>
#include <string>

#include "agent.h"
#include "ai_service.h"
#include "config.h"
#include "memory_manager.h"
#include "startup_trace.h"
#include "terminal_setup.h"
#include "tool_call.h"
#include "ui.h"

namespace {
// The earliest point a C++ program can report from. If the time is
// spent before main (the dynamic loader, static initializers), this
// line is the first thing in the trace and the gap to "main: entered"
// is that time.
struct StartupTraceInit {
  StartupTraceInit() { Utils::startup_trace("process: static initialization"); }
} startup_trace_init;
} // namespace

int main() {
  Utils::startup_trace("main: entered");
  try {
    // Initialize configuration (reads env vars, .env, etc.)
    Utils::startup_trace("main: loading environment");
    Utils::Config::load_environment();
    Utils::startup_trace("main: environment loaded");

    // Test hook: dump the tool registry and exit. The golden suite reads
    // this to build its sweep, so the list of tools under test comes
    // from the registry rather than a hand-maintained list.
    //
    // TEST_MODE is required as well: the dump is a test affordance, and
    // must not be reachable in a normal run.
    if (Utils::Config::get_env_var("AGENTWARE_DUMP_TOOLS", "") == "1" &&
        Utils::Config::get_env_var("TEST_MODE", "") == "1") {
      std::cout << Core::tool_registry_dump();
      return 0;
    }

    // Windows-only console bring-up: UTF-8 output code page + VT
    // processing on stdout/stderr; original state saved and restored
    // at exit and on Ctrl-C. A false return means the console cannot
    // do VT (legacy conhost): switch to the ASCII, colors-off glyph
    // table instead of printing mojibake boxes.
    Utils::startup_trace("main: terminal setup");
    if (!Utils::Terminal::setup()) {
      Utils::UI::enable_ascii_fallback();
    }
    Utils::Terminal::install_ctrl_c_restore();
    Utils::startup_trace("main: terminal ready");

    // Check for test mode is handled in Agent::initialize_mode()

    // Display welcome screen
    Utils::startup_trace("main: print logo");
    Utils::UI::print_logo();
    Utils::startup_trace("main: logo printed");

    // Create agent
    Core::Agent agent;
    Utils::startup_trace("main: agent constructed");

    // Run agent
    agent.run();
    Utils::startup_trace("main: agent run returned");

    std::cout << Utils::Color::DIM << "Agent run completed"
              << Utils::Color::RESET << std::endl;

  } catch (const std::exception &e) {
    std::cerr << Utils::Color::RED << "Fatal error: " << e.what()
              << Utils::Color::RESET << std::endl;
    return 1;
  }

  return 0;
}
