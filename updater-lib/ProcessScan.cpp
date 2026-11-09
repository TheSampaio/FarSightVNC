// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Process and in-use checks declared in Installer.h.

#include "Installer.h"

namespace fsv::update {

// STUB(worker): process enumeration (Toolhelp + QueryFullProcessImageNameW) is not written yet.
std::vector<std::uint32_t> findProcessesByImage(const std::vector<std::wstring> &,
                                                std::uint32_t) {
  return {};
}

// STUB(worker): waiting for processes is not written yet.
bool waitForProcessesToExit(const std::vector<std::wstring> &, const std::function<bool()> &,
                            std::uint32_t) {
  return false;
}

// STUB(worker): the exclusive-open probe is not written yet.
bool isFileInUse(const std::wstring &) { return false; }

} // namespace fsv::update
