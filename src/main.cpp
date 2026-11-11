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
#include "version.h"

namespace {
// The earliest point a C++ program can report from. If the time is
// spent before main (the dynamic loader, static initializers), this
// line is the first thing in the trace and the gap to "main: entered"
// is that time.
struct StartupTraceInit {
  StartupTraceInit() { Utils::startup_trace("process: static initialization"); }
} startup_trace_init;

struct ProviderFlag {
  const char *name;
  const char *env_var;
  Core::AgentMode mode;
};

// The single list of online providers a --provider value may name. It has
// to stay in step with the prompt table in Agent::initialize_mode.
constexpr ProviderFlag kProviderFlags[] = {
    {"together", "TOGETHER_API_KEY", Core::AgentMode::MODE_TOGETHER},
    {"cerebras", "CEREBRAS_API_KEY", Core::AgentMode::MODE_CEREBRAS},
    {"fireworks", "FIREWORKS_API_KEY", Core::AgentMode::MODE_FIREWORKS},
    {"groq", "GROQ_API_KEY", Core::AgentMode::MODE_GROQ},
    {"deepseek", "DEEPSEEK_API_KEY", Core::AgentMode::MODE_DEEPSEEK},
    {"openai", "OPENAI_API_KEY", Core::AgentMode::MODE_OPENAI},
};

struct ModelFlag {
  const char *name;
  Core::AgentMode mode;
};

// The models the offline prompt already offers; --model accepts nothing
// beyond this set because the model string is hardcoded per backend.
constexpr ModelFlag kModelFlags[] = {
    {"llama3.2:3b", Core::AgentMode::MODE_LLAMA_3B},
    {"llama3.2:latest", Core::AgentMode::MODE_LLAMA_LATEST},
    {"llama3.1:latest", Core::AgentMode::MODE_LLAMA_31},
};

void print_usage(std::ostream &out) {
  out << "Usage: agentware-agent [options]\n"
      << "\n"
      << "Options:\n"
      << "  -V, --version            Print version and build information, then "
         "exit\n"
      << "  -H, --help               Print this usage, then exit\n"
      << "  --mode <online|offline>  Start in this mode without the mode "
         "prompt\n"
      << "  --provider <name>        Online provider: together, cerebras, "
         "fireworks,\n"
      << "                           groq, deepseek, openai (requires --mode "
         "online)\n"
      << "  --model <name>           Offline model: llama3.2:3b, "
         "llama3.2:latest,\n"
      << "                           llama3.1:latest (requires --mode "
         "offline)\n"
      << "\n"
      << "Without --mode the agent prompts for the mode as before. With "
         "--mode but\n"
      << "no provider or model, only the remaining prompt is shown.\n";
}

enum class ArgAction { Run, Version, Help, Error };

struct ParsedArgs {
  ArgAction action = ArgAction::Run;
  Core::StartupOptions options;
  std::string error;
};

const ProviderFlag *find_provider(const std::string &name) {
  for (const auto &provider : kProviderFlags) {
    if (name == provider.name)
      return &provider;
  }
  return nullptr;
}

const ProviderFlag *provider_for_mode(Core::AgentMode mode) {
  for (const auto &provider : kProviderFlags) {
    if (provider.mode == mode)
      return &provider;
  }
  return nullptr;
}

const ModelFlag *find_model(const std::string &name) {
  for (const auto &model : kModelFlags) {
    if (name == model.name)
      return &model;
  }
  return nullptr;
}

ParsedArgs parse_args(int argc, char **argv) {
  ParsedArgs parsed;
  bool mode_set = false;
  bool online = false;
  bool provider_set = false;
  bool model_set = false;
  std::string provider_name;
  std::string model_name;

  auto fail = [&](const std::string &message) {
    parsed.action = ArgAction::Error;
    parsed.error = message;
    return parsed;
  };

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version" || arg == "-V") {
      parsed.action = ArgAction::Version;
      return parsed;
    }
    if (arg == "--help" || arg == "-H") {
      parsed.action = ArgAction::Help;
      return parsed;
    }
    if (arg == "--mode" || arg == "--provider" || arg == "--model") {
      if (i + 1 >= argc)
        return fail("missing value for " + arg);
      const std::string value = argv[i + 1];
      if (!value.empty() && value[0] == '-')
        return fail("missing value for " + arg);
      ++i;
      if (arg == "--mode") {
        if (value != "online" && value != "offline")
          return fail("invalid mode: " + value +
                      " (expected online or offline)");
        mode_set = true;
        online = (value == "online");
      } else if (arg == "--provider") {
        if (find_provider(value) == nullptr)
          return fail("unknown provider: " + value);
        provider_set = true;
        provider_name = value;
      } else {
        if (find_model(value) == nullptr)
          return fail("unknown model: " + value);
        model_set = true;
        model_name = value;
      }
      continue;
    }
    return fail("unknown option: " + arg);
  }

  if (provider_set && !mode_set)
    return fail("--provider requires --mode online");
  if (model_set && !mode_set)
    return fail("--model requires --mode offline");
  if (mode_set && online && model_set)
    return fail("--model is offline-only; use --mode offline");
  if (mode_set && !online && provider_set)
    return fail("--provider is online-only; use --mode online");

  parsed.options.mode_selected = mode_set;
  parsed.options.online = online;
  if (mode_set && provider_set)
    parsed.options.selection = find_provider(provider_name)->mode;
  if (mode_set && model_set)
    parsed.options.selection = find_model(model_name)->mode;
  return parsed;
}
} // namespace

int main(int argc, char **argv) {
  Utils::startup_trace("main: entered");

  const ParsedArgs parsed = parse_args(argc, argv);
  if (parsed.action == ArgAction::Help) {
    print_usage(std::cout);
    return 0;
  }
  if (parsed.action == ArgAction::Version) {
    Version::print_version_info();
    return 0;
  }
  if (parsed.action == ArgAction::Error) {
    std::cerr << "Error: " << parsed.error << "\n\n";
    print_usage(std::cerr);
    return 2;
  }

  try {
    // Initialize configuration (reads env vars, .env, etc.)
    Utils::startup_trace("main: loading environment");
    Utils::Config::load_environment();
    Utils::startup_trace("main: environment loaded");

    // Fail before any boot prompt when an explicit online provider has no
    // credential. A silent fallback would start the REPL in a mode the user
    // did not ask for.
    if (parsed.options.mode_selected && parsed.options.online &&
        parsed.options.selection != Core::AgentMode::MODE_UNSET) {
      const ProviderFlag *provider =
          provider_for_mode(parsed.options.selection);
      if (provider != nullptr &&
          Utils::Config::get_env_var(provider->env_var).empty()) {
        std::cerr << "Error: " << provider->env_var
                  << " is not set (required by --provider " << provider->name
                  << ")\n";
        return 1;
      }
    }

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
    agent.run(parsed.options);
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
