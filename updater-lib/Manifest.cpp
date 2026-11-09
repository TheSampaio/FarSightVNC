// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Manifest.h"

namespace fsv::update {

// STUB(worker): relative path validation is not written yet.
bool isSafeRelativePath(std::string_view) { return false; }

// STUB(worker): manifest grammar parser is not written yet.
std::optional<Manifest> parseManifest(std::string_view) { return std::nullopt; }

// STUB(worker): signature check + parse is not written yet.
std::optional<Manifest> verifyAndParseManifest(std::span<const std::uint8_t>,
                                               std::span<const std::uint8_t>,
                                               std::span<const TrustedKey>,
                                               ManifestError *error) {
  if (error != nullptr) {
    *error = ManifestError::Malformed;
  }
  return std::nullopt;
}

} // namespace fsv::update
