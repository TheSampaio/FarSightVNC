// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Installer.h"

namespace fsv::update {

// STUB(worker): layout detection is not written yet.
InstallTarget detectInstallTarget(const std::wstring &, std::string_view arch) {
  InstallTarget target;
  target.layout = InstallLayout::Standalone;
  target.arch = std::string(arch);
  return target;
}

// STUB(worker): install planning is not written yet.
std::vector<PlannedFile> planInstall(const Manifest &, const std::wstring &,
                                     const InstallTarget &) {
  return {};
}

// STUB(worker): viewer executable list is not written yet.
std::vector<std::wstring> viewerExecutables(const std::vector<PlannedFile> &) { return {}; }

// STUB(worker): names of the InstallError values are not written yet.
const char *installErrorName(InstallError) { return "unimplemented"; }

// STUB(worker): the install transaction is not written yet.
InstallError applyInstall(const InstallTarget &, const std::vector<PlannedFile> &,
                          std::string *detail, const InstallHooks *) {
  if (detail != nullptr) {
    *detail = "not implemented";
  }
  return InstallError::Io;
}

// STUB(worker): journal detection is not written yet.
bool hasPendingJournal(const std::wstring &) { return false; }

// STUB(worker): crash recovery is not written yet.
InstallError recoverInstall(const std::wstring &, std::string *detail) {
  if (detail != nullptr) {
    *detail = "not implemented";
  }
  return InstallError::Io;
}

} // namespace fsv::update
