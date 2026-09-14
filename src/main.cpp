#include <cstdlib>
#include <iostream>
#include <string>

#include "agent.h"
#include "memory_manager.h"
#include "ai_service.h"
#include "config.h"
#include "ui.h"

int main() {
  try {
    // Initialize configuration (reads env vars, .env, etc.)
    Utils::Config::load_environment();

    // Check for test mode is handled in Agent::initialize_mode()

    // Display welcome screen
    Utils::UI::print_logo();

    // Create agent
    Core::Agent agent;

    // Run agent
    agent.run();

    std::cout << "Agent run completed" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << Utils::Color::RED << "Fatal error: " << e.what()
              << Utils::Color::RESET << std::endl;
    return 1;
  }

  return 0;
}
