// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "UpdateService.h"

#include <utility>

namespace fsv::update {

UpdateSettingsStore::UpdateSettingsStore(std::wstring subKey) : m_subKey(std::move(subKey)) {}

// STUB(worker): registry reading is not written yet.
UpdateSettings UpdateSettingsStore::load() const { return UpdateSettings{}; }

// STUB(worker): registry writing is not written yet.
bool UpdateSettingsStore::save(const UpdateSettings &) const { return false; }

// STUB(worker): HKLM policy lookup is not written yet.
bool updatesDisabledByPolicy() { return false; }

// STUB(worker): channel default is not written yet.
bool effectiveIncludePrereleases(const UpdateSettings &, const Version &) { return false; }

// STUB(worker): GitHub check is not written yet.
CheckResult checkForUpdate(IHttpClient &, const SelectionOptions &) {
  CheckResult result;
  result.kind = CheckResult::Kind::Failed;
  result.detail = "not implemented";
  return result;
}

// STUB(worker): check policy is not written yet.
bool isAutomaticCheckDue(const UpdateSettings &, std::uint64_t) { return false; }

// STUB(worker): check bookkeeping is not written yet.
void recordCheckOutcome(UpdateSettings &, const CheckResult &, std::uint64_t) {}

// STUB(worker): names of the StageError values are not written yet.
const char *stageErrorName(StageError) { return "unimplemented"; }

// STUB(worker): staging (download, verify, extract) is not written yet.
StageResult stageUpdate(IHttpClient &, const ReleaseInfo &, const Version &,
                        std::span<const TrustedKey>, const std::wstring &, const ProgressFn &) {
  StageResult result;
  result.error = StageError::Io;
  result.detail = "not implemented";
  return result;
}

// STUB(worker): re-verification of a staged update is not written yet.
std::optional<Manifest> loadStagedUpdate(const std::wstring &, std::span<const TrustedKey>) {
  return std::nullopt;
}

} // namespace fsv::update
