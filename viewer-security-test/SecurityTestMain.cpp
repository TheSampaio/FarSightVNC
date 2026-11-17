// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a modified version of TightVNC.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Entry point of the security tests: it owns the pass/fail counters and runs
// one runner per area. Every test in this program is deterministic and offline:
// no file is created, written or deleted, no network and no VNC server are
// involved.
//

#include "viewer-security-test/SecurityTestHarness.h"

#include <stdio.h>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool ok, const char *what)
{
  if (ok) {
    g_passed++;
  } else {
    g_failed++;
    printf("FAIL: %s\n", what);
  }
}

} // namespace

//----------------------------------------------------------------------------
// The runners, one per area, each in its own translation unit.

// FileNameRulesTest.cpp: util/WindowsFileName.{h,cpp}.
void runFileNameRulesTests(int *passed, int *failed);

// Hook for the later phases of the security work. Each of the three runners
// below belongs to one worker and to one new .cpp file in this directory; none
// of them exists yet, so none of them is declared or called yet. A worker that
// adds its file:
//   1. adds the source to viewer-security-test.vcxproj (and to its filters
//      file, if one has been added by then) and to CMakeLists.txt;
//   2. declares its runner next to runFileNameRulesTests above;
//   3. adds one RUN_AREA line for it in main below.
// The runners, in the order the areas were planned:
//   void runFileTransferPathTests(int *passed, int *failed);  // FileTransferPathTest.cpp
//   void runFileTransferWireTests(int *passed, int *failed);  // FileTransferWireTest.cpp
//   void runRfbLimitsTests(int *passed, int *failed);         // RfbLimitsTest.cpp
// Nothing else in this file has to change: the counters, the summary line and
// the exit code already cover every area.

int main()
{
  setvbuf(stdout, NULL, _IONBF, 0);
  // The harness itself must count, or a silent runner would look like a pass.
  check(g_failed == 0, "harness: the counters start empty");
#define RUN_AREA(r) (printf("RUN " #r "\n"), r(&g_passed, &g_failed))
  RUN_AREA(runFileNameRulesTests);
#undef RUN_AREA
  printf("viewer-security-test: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
