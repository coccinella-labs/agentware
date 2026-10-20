#include <cstdlib>
#include <iostream>
#include <string>

#include "agent.h"
#include "ai_service.h"
#include "config.h"
#include "memory_manager.h"
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

    std::cout << Utils::Color::DIM << "Agent run completed"
              << Utils::Color::RESET << std::endl;

  } catch (const std::exception &e) {
    std::cerr << Utils::Color::RED << "Fatal error: " << e.what()
              << Utils::Color::RESET << std::endl;
    return 1;
  }

  return 0;
}
