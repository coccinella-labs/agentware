#include "config.h"

// Define the AGENTWARE_LIBRARY macro to ensure proper symbol export
#ifndef AGENTWARE_LIBRARY
#define AGENTWARE_LIBRARY
#endif
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Utils {
namespace Config {

static void set_env_var(const std::string &key, const std::string &value) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), value.c_str());
#else
  setenv(key.c_str(), value.c_str(), 1);
#endif
}

void load_environment(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    return; // Optional
  }

  std::string line;
  while (std::getline(file, line)) {
    // Trim leading/trailing spaces
    auto trim = [](std::string &s) {
      s.erase(0, s.find_first_not_of(" \t\r\n"));
      s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };

    trim(line);
    if (line.empty() || line[0] == '#')
      continue;

    // Remove inline comment if not inside quotes
    size_t hash_pos = line.find('#');
    if (hash_pos != std::string::npos && (line.find('"') > hash_pos)) {
      line = line.substr(0, hash_pos);
      trim(line);
    }

    std::istringstream is_line(line);
    std::string key, value;

    if (std::getline(is_line, key, '=') && std::getline(is_line, value)) {
      trim(key);
      trim(value);

      // Remove quotes
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }

      if (!key.empty()) {
        set_env_var(key, value);
      }
    }
  }
}

std::string get_env_var(const std::string &key,
                        const std::string &default_value) {
  const char *value = std::getenv(key.c_str());
  return value ? std::string(value) : default_value;
}

bool has_env_var(const std::string &key) {
  return std::getenv(key.c_str()) != nullptr;
}
} // namespace Config
} // namespace Utils
