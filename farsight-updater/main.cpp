// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// FarSightUpdater.exe: waits for the viewer processes of one installation to
// exit, then installs a staged, signed update (see docs/updater/DESIGN.md).

#include <windows.h>

namespace {

// Exit codes of FarSightUpdater.exe.
constexpr int kExitNotImplemented = 2;

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  // First thing: never load DLLs from the application or current directory.
  ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

  // STUB(worker): the install flow (mutex, loadStagedUpdate, wait, elevate,
  // applyInstall, recovery) is not written yet.
  return kExitNotImplemented;
}
