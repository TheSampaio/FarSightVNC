// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "ZipReader.h"

namespace fsv::update {

// STUB(worker): ZIP central directory reading and inflate (vendor zlib) are not written yet.
bool extractManifestFiles(const std::wstring &, const Manifest &, const std::wstring &,
                          std::string *error) {
  if (error != nullptr) {
    *error = "not implemented";
  }
  return false;
}

} // namespace fsv::update
