// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Releases.h"

namespace fsv::update {

const ReleaseAsset *ReleaseInfo::findAsset(std::string_view name) const {
  for (const ReleaseAsset &asset : assets) {
    if (asset.name == name) {
      return &asset;
    }
  }
  return nullptr;
}

// STUB(worker): GitHub releases JSON parsing is not written yet.
std::optional<std::vector<ReleaseInfo>> parseGitHubReleases(std::string_view) {
  return std::nullopt;
}

// STUB(worker): prerelease classification is not written yet.
bool isPrereleaseRelease(const ReleaseInfo &) { return false; }

// STUB(worker): update selection is not written yet.
const ReleaseInfo *selectUpdate(const std::vector<ReleaseInfo> &, const SelectionOptions &) {
  return nullptr;
}

} // namespace fsv::update
