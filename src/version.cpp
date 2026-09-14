#include "version.h"
#include <iostream>
#include <string>

namespace Version {
const char *get_version() { return agentware_version_string; }

const char *get_build_info() {
  static std::string build_info;
  if (build_info.empty()) {
    build_info = std::string(__DATE__) + " " + __TIME__;
  }
  return build_info.c_str();
}

void print_version_info() {
  std::cout << "Agentware v" << get_version() << "\n";
  std::cout << "Built: " << get_build_info() << "\n";
}
} // namespace Version
