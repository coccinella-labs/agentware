#include "command_router.h"

#include <cctype>
#include <sstream>

namespace Core {

bool matches_bare_verb(const std::string &input, const std::string &verb) {
  return input == verb;
}

bool has_prefix_ci(const std::string &input, const std::string &prefix) {
  if (input.size() < prefix.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(input[i])) !=
        static_cast<unsigned char>(prefix[i])) {
      return false;
    }
  }
  return true;
}

bool is_file_reference_at(const std::string &input, size_t pos) {
  if (pos >= input.size() || input[pos] != '@') {
    return false;
  }
  if (pos == 0) {
    return true;
  }
  const char prev = input[pos - 1];
  return prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r' ||
         prev == '"' || prev == '(' || prev == '[';
}

InputRoute classify_input(const std::string &trimmed) {
  // Explicit command prefixes win outright. An at-sign inside a command must
  // not divert it, so '!', '/', the bare verbs, and colon commands are
  // considered before any file reference.
  if (trimmed.rfind("!", 0) == 0) {
    return InputRoute::Shell;
  }
  if (trimmed.rfind("/", 0) == 0) {
    return InputRoute::Meta;
  }
  if (matches_bare_verb(trimmed, "forget") ||
      matches_bare_verb(trimmed, "memory") ||
      matches_bare_verb(trimmed, "clear")) {
    return InputRoute::DirectCommand;
  }
  if (trimmed.find(':') != std::string::npos) {
    return InputRoute::DirectCommand;
  }
  for (size_t pos = 0; pos < trimmed.size(); ++pos) {
    if (is_file_reference_at(trimmed, pos)) {
      return InputRoute::FileInjection;
    }
  }
  return InputRoute::Chat;
}

std::string redact_secrets(const std::string &input) {
  // "/auth key <provider> <secret>" must not reach persisted state. Only the
  // leading provider token is kept; everything after it is dropped.
  const std::string marker = "/auth key ";
  if (input.rfind(marker, 0) != 0) {
    return input;
  }
  std::istringstream iss(input.substr(marker.size()));
  std::string provider;
  iss >> provider;
  if (provider.empty()) {
    return input;
  }
  return marker + provider + " <redacted>";
}

} // namespace Core
