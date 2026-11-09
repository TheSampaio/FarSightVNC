// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// updater-test: deterministic tests of updater-lib. Exit code 0 = all passed.

#include "TestHarness.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <exception>

void runVersionTests();
void runCryptoTests();
void runFsUtilTests();
void runJsonTests();
void runReleasesTests();
void runManifestTests();
void runZipTests();
void runUpdateServiceTests();
void runInstallerTests();
void runHttpTests();

namespace {

void runSuite(void (*suite)()) {
  try {
    suite();
  } catch (const std::exception &error) {
    fsv::test::recordResult(false, "suite threw an exception", __FILE__, __LINE__);
    std::printf("  exception: %s\n", error.what());
  } catch (...) {
    fsv::test::recordResult(false, "suite threw an unknown exception", __FILE__, __LINE__);
  }
}

} // namespace

int main(int argc, char **argv) {
  // Helper mode: copies of this exe act as fake "tvnviewer.exe" processes.
  if (argc >= 3 && std::strcmp(argv[1], "--sleep-ms") == 0) {
    ::Sleep(static_cast<DWORD>(std::atol(argv[2])));
    return 0;
  }
  runSuite(runVersionTests);
  runSuite(runCryptoTests);
  runSuite(runFsUtilTests);
  runSuite(runJsonTests);
  runSuite(runReleasesTests);
  runSuite(runManifestTests);
  runSuite(runZipTests);
  runSuite(runUpdateServiceTests);
  runSuite(runInstallerTests);
  runSuite(runHttpTests);

  const fsv::test::Counters &counters = fsv::test::counters();
  std::printf("updater-test: %d suites, %d checks, %d failures, %d notes\n", counters.suites,
              counters.checks, counters.failures, counters.notes);
  return counters.failures == 0 ? 0 : 1;
}
