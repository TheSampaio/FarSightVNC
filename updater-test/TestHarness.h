// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Hand-rolled test harness (same style as viewer-favorites-test): CHECK
// records failures with file:line and the process exit code is 0 only if
// every check passed.

#pragma once

#include <cstdio>
#include <string>

namespace fsv::test {

struct Counters {
  int suites = 0;
  int checks = 0;
  int failures = 0;
  int notes = 0;
};

inline Counters &counters() {
  static Counters instance;
  return instance;
}

inline void beginSuite(const char *name) {
  ++counters().suites;
  std::printf("[suite] %s\n", name);
}

// Informational line (for example a skipped check); never a failure.
inline void note(const char *text) {
  ++counters().notes;
  std::printf("  note: %s\n", text);
}

inline void recordResult(bool passed, const char *expression, const char *file, int line) {
  ++counters().checks;
  if (!passed) {
    ++counters().failures;
    std::printf("  FAIL %s:%d: %s\n", file, line, expression);
    std::fflush(stdout);
  }
}

} // namespace fsv::test

// Variadic so that expressions with top-level commas (braced initializers) work.
#define CHECK(...) \
  ::fsv::test::recordResult(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)
