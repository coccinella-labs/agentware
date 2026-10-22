// Startup tracing.
//
// Appends timestamped lines to the file named by AGENTWARE_STARTUP_TRACE.
// It is off unless that variable is set, so a normal run pays nothing:
// the check is one getenv and one branch.
//
// The point is to answer "where does the startup time go". A step that
// blocks for seconds (a keychain read, a name lookup, a connect to a
// port nobody is listening on) is invisible in the normal output, because
// the agent prints its banner before it does any of it.
//
// The file is written and not stdout on purpose: the golden suites
// assert on stdout, and a trace line in the output would be a marker
// they could trip over.
//
// Usage:
//   AGENTWARE_STARTUP_TRACE=/tmp/trace.log ./build/bin/agentware-agent
#ifndef AGENTWARE_STARTUP_TRACE_H
#define AGENTWARE_STARTUP_TRACE_H

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace Utils {

// One trace line: wall clock, milliseconds since the first line, and the
// step name.
inline void startup_trace(const std::string &step) {
  static const char *path = std::getenv("AGENTWARE_STARTUP_TRACE");
  if (path == nullptr || *path == '\0') {
    return;
  }
  static const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const double elapsed_ms =
      std::chrono::duration<double, std::milli>(now - start).count();
  const auto wall = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &wall);
#else
  localtime_r(&wall, &parts);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &parts);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          now.time_since_epoch())
                          .count() %
                      1000000;
  FILE *out = std::fopen(path, "a");
  if (out == nullptr) {
    return;
  }
  std::fprintf(out, "%s.%06ld  +%9.1fms  %s\n", stamp,
               static_cast<long>(micros), elapsed_ms, step.c_str());
  std::fclose(out);
}

} // namespace Utils

#endif // AGENTWARE_STARTUP_TRACE_H
