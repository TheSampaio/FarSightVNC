// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Viewer-side update operations: settings and policy, the GitHub check, and
// staging. Nothing here executes downloaded content; staged files are only
// written after the signed manifest and every hash have been verified.

#include "UpdateService.h"

#include "FsUtil.h"
#include "Win32Util.h"
#include "ZipReader.h"

#include <algorithm>
#include <utility>

namespace fsv::update {

namespace {

// ---------------------------------------------------------------- registry

constexpr wchar_t kAutoCheckValue[] = L"AutoCheck";
constexpr wchar_t kIncludePrereleasesValue[] = L"IncludePrereleases";
constexpr wchar_t kSkippedVersionValue[] = L"SkippedVersion";
constexpr wchar_t kLastCheckValue[] = L"LastCheck";
constexpr wchar_t kNextCheckValue[] = L"NextCheckNotBefore";
constexpr wchar_t kPolicySubKey[] = L"SOFTWARE\\Policies\\FarSightVNC\\Viewer\\Updater";
constexpr wchar_t kPolicyValue[] = L"DisableUpdateCheck";
constexpr DWORD kMaxVersionStringChars = 32;

class RegistryKey {
public:
  RegistryKey() = default;
  ~RegistryKey() {
    if (m_key != nullptr) {
      ::RegCloseKey(m_key);
    }
  }
  RegistryKey(const RegistryKey &) = delete;
  RegistryKey &operator=(const RegistryKey &) = delete;

  bool open(HKEY root, const wchar_t *subKey, REGSAM access) {
    return ::RegOpenKeyExW(root, subKey, 0, access, &m_key) == ERROR_SUCCESS;
  }
  bool create(HKEY root, const wchar_t *subKey, REGSAM access) {
    return ::RegCreateKeyExW(root, subKey, 0, nullptr, 0, access, nullptr, &m_key, nullptr) ==
           ERROR_SUCCESS;
  }
  HKEY get() const { return m_key; }

private:
  HKEY m_key = nullptr;
};

std::optional<std::uint32_t> readDword(HKEY key, const wchar_t *name) {
  DWORD type = 0;
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) !=
          ERROR_SUCCESS ||
      type != REG_DWORD || size != sizeof(value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint64_t> readQword(HKEY key, const wchar_t *name) {
  DWORD type = 0;
  std::uint64_t value = 0;
  DWORD size = sizeof(value);
  if (::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) !=
          ERROR_SUCCESS ||
      type != REG_QWORD || size != sizeof(value)) {
    return std::nullopt;
  }
  return value;
}

// Short REG_SZ only (a version string); anything longer is treated as invalid.
std::optional<std::wstring> readShortString(HKEY key, const wchar_t *name) {
  wchar_t buffer[kMaxVersionStringChars + 1] = {};
  DWORD type = 0;
  DWORD size = kMaxVersionStringChars * sizeof(wchar_t);
  if (::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) !=
          ERROR_SUCCESS ||
      type != REG_SZ) {
    return std::nullopt;
  }
  buffer[kMaxVersionStringChars] = L'\0';
  return std::wstring(buffer);
}

bool writeDword(HKEY key, const wchar_t *name, std::uint32_t value) {
  return ::RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value),
                          sizeof(value)) == ERROR_SUCCESS;
}

bool writeQword(HKEY key, const wchar_t *name, std::uint64_t value) {
  return ::RegSetValueExW(key, name, 0, REG_QWORD, reinterpret_cast<const BYTE *>(&value),
                          sizeof(value)) == ERROR_SUCCESS;
}

bool writeString(HKEY key, const wchar_t *name, const std::wstring &value) {
  return ::RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ==
         ERROR_SUCCESS;
}

bool deleteValue(HKEY key, const wchar_t *name) {
  const LONG result = ::RegDeleteValueW(key, name);
  return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

// ----------------------------------------------------------------- staging

constexpr char kStageAccept[] = "application/octet-stream";
constexpr std::uint64_t kStageSpaceMargin = 1024 * 1024;
constexpr wchar_t kFilesDirName[] = L"files";
constexpr wchar_t kPackagePartName[] = L"package.zip.part";
constexpr wchar_t kPackageName[] = L"package.zip";
constexpr wchar_t kManifestFileName[] = L"farsightvnc-update.manifest";
constexpr wchar_t kSignatureFileName[] = L"farsightvnc-update.manifest.sig";

StageResult stageFailure(StageError error, std::string detail,
                         HttpError httpError = HttpError::None) {
  StageResult result;
  result.error = error;
  result.httpError = httpError;
  result.detail = std::move(detail);
  return result;
}

StageResult networkFailure(HttpError error, const char *what) {
  if (error == HttpError::Cancelled) {
    return stageFailure(StageError::Cancelled, "cancelled", error);
  }
  return stageFailure(StageError::Network, std::string(what) + ": " + httpErrorName(error), error);
}

std::span<const std::uint8_t> bytesOf(const std::string &text) {
  return {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()};
}

// Release assets must come from this release of the official repository, and
// the file name must be the last path segment (no traversal, query or fragment).
bool assetUrlIsPinned(const ReleaseAsset &asset, const std::string &tag) {
  const std::string prefix = std::string(kAssetUrlPrefix) + tag + "/";
  return asset.downloadUrl.size() == prefix.size() + asset.name.size() &&
         asset.downloadUrl.compare(0, prefix.size(), prefix) == 0 &&
         asset.downloadUrl.compare(prefix.size(), std::string::npos, asset.name) == 0;
}

// Removes the pending directory on destruction unless released.
class PendingCleanup {
public:
  PendingCleanup(std::wstring pending, std::wstring stateDir)
      : m_pending(std::move(pending)), m_stateDir(std::move(stateDir)) {}
  ~PendingCleanup() {
    if (m_armed) {
      removeTree(m_pending, m_stateDir);
    }
  }
  PendingCleanup(const PendingCleanup &) = delete;
  PendingCleanup &operator=(const PendingCleanup &) = delete;
  void release() { m_armed = false; }

private:
  std::wstring m_pending;
  std::wstring m_stateDir;
  bool m_armed = true;
};

StageResult stageUpdateImpl(IHttpClient &http, const ReleaseInfo &release, const Version &current,
                            std::span<const TrustedKey> keys, const std::wstring &stateDir,
                            const ProgressFn &progress) {
  if (stateDir.empty()) {
    return stageFailure(StageError::Io, "no state directory");
  }

  // 1. Manifest and signature assets (the package is named by the manifest).
  const ReleaseAsset *manifestAsset = release.findAsset(kManifestAssetName);
  const ReleaseAsset *signatureAsset = release.findAsset(kSignatureAssetName);
  if (manifestAsset == nullptr || signatureAsset == nullptr) {
    return stageFailure(StageError::MissingAssets, "release has no signed manifest");
  }
  if (!assetUrlIsPinned(*manifestAsset, release.tag) ||
      !assetUrlIsPinned(*signatureAsset, release.tag)) {
    return stageFailure(StageError::BadAssetUrl, "asset URL is outside the official release");
  }

  // 2. Fresh pending directory.
  const std::wstring pending = joinPath(stateDir, kPendingDirName);
  if (!removeTree(pending, stateDir) || !ensureDirectory(pending)) {
    return stageFailure(StageError::Io, "cannot prepare the pending directory");
  }
  PendingCleanup cleanup(pending, stateDir);

  // 3. Manifest + signature into memory, then verify before using anything.
  const Headers headers{{"Accept", kStageAccept}};
  HttpResponse manifestResponse;
  if (const HttpError error = http.get(manifestAsset->downloadUrl, headers, kMaxManifestBytes,
                                       manifestResponse);
      error != HttpError::None) {
    if (error == HttpError::TooLarge) {
      return stageFailure(StageError::TooLarge, "manifest is too large", error);
    }
    return networkFailure(error, "manifest download");
  }
  HttpResponse signatureResponse;
  if (const HttpError error = http.get(signatureAsset->downloadUrl, headers, kSignatureBytes,
                                       signatureResponse);
      error == HttpError::TooLarge) {
    return stageFailure(StageError::BadSignature, "signature has the wrong size", error);
  } else if (error != HttpError::None) {
    return networkFailure(error, "signature download");
  }
  if (signatureResponse.body.size() != kSignatureBytes) {
    return stageFailure(StageError::BadSignature, "signature has the wrong size");
  }

  ManifestError manifestError = ManifestError::None;
  const std::optional<Manifest> parsed =
      verifyAndParseManifest(bytesOf(manifestResponse.body), bytesOf(signatureResponse.body), keys,
                             &manifestError);
  if (!parsed) {
    switch (manifestError) {
    case ManifestError::TooLarge:
      return stageFailure(StageError::TooLarge, "manifest is too large");
    case ManifestError::Malformed:
      return stageFailure(StageError::Malformed, "manifest is malformed");
    default:
      return stageFailure(StageError::BadSignature, "manifest signature is invalid");
    }
  }
  const Manifest &manifest = *parsed;

  // 4. Version binding: the signed version must be the tag and a real upgrade.
  if (manifest.version != release.version) {
    return stageFailure(StageError::VersionMismatch, "manifest version differs from the tag");
  }
  if (manifest.version <= current) {
    return stageFailure(StageError::NotNewer, "manifest version is not newer than the running one");
  }

  const ReleaseAsset *packageAsset = release.findAsset(manifest.packageName);
  if (packageAsset == nullptr) {
    return stageFailure(StageError::MissingAssets, "release has no package asset");
  }
  if (!assetUrlIsPinned(*packageAsset, release.tag)) {
    return stageFailure(StageError::BadAssetUrl, "package URL is outside the official release");
  }

  // 5. Disk space for the package, its extracted files and a margin.
  std::uint64_t required = manifest.packageSize + kStageSpaceMargin;
  for (const ManifestFile &file : manifest.files) {
    required += file.size;
  }
  if (const std::optional<std::uint64_t> free = freeSpaceBytes(stateDir); free && *free < required) {
    return stageFailure(StageError::NoSpace, "not enough free disk space");
  }

  // 6. Package, bounded by the signed size and checked against the signed hash.
  const std::wstring packagePart = joinPath(pending, kPackagePartName);
  const std::wstring packageZip = joinPath(pending, kPackageName);
  const std::uint64_t packageSize = manifest.packageSize;
  const ProgressFn forward = [&](std::uint64_t received, std::uint64_t total) {
    return !progress || progress(received, total != 0 ? total : packageSize);
  };
  HttpResponse packageResponse;
  if (const HttpError error = http.download(packageAsset->downloadUrl, packagePart, packageSize,
                                            forward, packageResponse);
      error == HttpError::TooLarge) {
    ::DeleteFileW(packagePart.c_str());
    return stageFailure(StageError::IntegrityFailed, "package is larger than the manifest size",
                        error);
  } else if (error != HttpError::None) {
    ::DeleteFileW(packagePart.c_str());
    return networkFailure(error, "package download");
  }
  std::uint64_t downloaded = 0;
  const std::optional<Sha256Digest> digest = sha256File(packagePart, &downloaded);
  if (!digest || downloaded != packageSize || *digest != manifest.packageSha256) {
    ::DeleteFileW(packagePart.c_str());
    return stageFailure(StageError::IntegrityFailed, "package size or SHA-256 does not match");
  }
  if (!::MoveFileExW(packagePart.c_str(), packageZip.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    return stageFailure(StageError::Io, "cannot finalize the downloaded package");
  }

  // 7. Extract exactly the manifest's files.
  std::string extractError;
  if (!extractManifestFiles(packageZip, manifest, joinPath(pending, kFilesDirName),
                            &extractError)) {
    return stageFailure(StageError::ExtractFailed, extractError);
  }

  // 8. Keep the verified manifest, drop the zip, and write the marker last.
  if (!writeFileAtomic(joinPath(pending, kManifestFileName), manifestResponse.body) ||
      !writeFileAtomic(joinPath(pending, kSignatureFileName), signatureResponse.body)) {
    return stageFailure(StageError::Io, "cannot write the staged manifest");
  }
  ::DeleteFileW(packageZip.c_str());
  if (!writeFileAtomic(joinPath(pending, kReadyMarkerName), manifest.version.toString())) {
    return stageFailure(StageError::Io, "cannot write the ready marker");
  }
  cleanup.release();

  StageResult result;
  result.error = StageError::None;
  result.manifest = manifest;
  return result;
}

} // namespace

// ---------------------------------------------------------------- settings

UpdateSettingsStore::UpdateSettingsStore(std::wstring subKey) : m_subKey(std::move(subKey)) {}

UpdateSettings UpdateSettingsStore::load() const {
  return guarded(
      [&] {
        UpdateSettings settings;
        RegistryKey key;
        if (m_subKey.empty() || !key.open(HKEY_CURRENT_USER, m_subKey.c_str(), KEY_QUERY_VALUE)) {
          return settings;
        }
        if (const auto value = readDword(key.get(), kAutoCheckValue)) {
          settings.autoCheck = *value != 0;
        }
        if (const auto value = readDword(key.get(), kIncludePrereleasesValue)) {
          settings.includePrereleases = *value != 0;
        }
        if (const auto text = readShortString(key.get(), kSkippedVersionValue)) {
          settings.skipped = Version::parse(wideToUtf8(*text));
        }
        settings.lastCheckUnix = readQword(key.get(), kLastCheckValue).value_or(0);
        settings.nextCheckNotBeforeUnix = readQword(key.get(), kNextCheckValue).value_or(0);
        return settings;
      },
      UpdateSettings{});
}

bool UpdateSettingsStore::save(const UpdateSettings &settings) const {
  return guarded(
      [&] {
        RegistryKey key;
        if (m_subKey.empty() || !key.create(HKEY_CURRENT_USER, m_subKey.c_str(), KEY_SET_VALUE)) {
          return false;
        }
        bool ok = writeDword(key.get(), kAutoCheckValue, settings.autoCheck ? 1 : 0);
        ok = (settings.includePrereleases
                  ? writeDword(key.get(), kIncludePrereleasesValue,
                               *settings.includePrereleases ? 1 : 0)
                  : deleteValue(key.get(), kIncludePrereleasesValue)) &&
             ok;
        ok = (settings.skipped ? writeString(key.get(), kSkippedVersionValue,
                                             utf8ToWide(settings.skipped->toString()))
                               : deleteValue(key.get(), kSkippedVersionValue)) &&
             ok;
        ok = writeQword(key.get(), kLastCheckValue, settings.lastCheckUnix) && ok;
        ok = writeQword(key.get(), kNextCheckValue, settings.nextCheckNotBeforeUnix) && ok;
        return ok;
      },
      false);
}

bool updatesDisabledByPolicy() {
  return guarded(
      [] {
        // Always the 64-bit view so Win32 and x64 builds read the same policy.
        RegistryKey key;
        if (!key.open(HKEY_LOCAL_MACHINE, kPolicySubKey, KEY_QUERY_VALUE | KEY_WOW64_64KEY)) {
          return false;
        }
        return readDword(key.get(), kPolicyValue) == std::optional<std::uint32_t>(1);
      },
      false);
}

bool effectiveIncludePrereleases(const UpdateSettings &settings, const Version &current) {
  return settings.includePrereleases.value_or(current.isPrerelease());
}

// ------------------------------------------------------------------- check

CheckResult checkForUpdate(IHttpClient &http, const SelectionOptions &options) {
  return guarded(
      [&] {
        CheckResult result;
        const Headers headers{{"Accept", "application/vnd.github+json"},
                              {"X-GitHub-Api-Version", "2022-11-28"}};
        constexpr std::size_t kMaxApiBytes = 2 * 1024 * 1024;
        HttpResponse response;
        const HttpError error = http.get(kReleasesApiUrl, headers, kMaxApiBytes, response);
        result.httpError = error;
        if (error == HttpError::RateLimited) {
          result.kind = CheckResult::Kind::RateLimited;
          result.retryAfterUnix = response.rateLimitResetUnix;
          result.detail = "GitHub rate limit reached";
          return result;
        }
        if (error != HttpError::None) {
          result.kind = CheckResult::Kind::Failed;
          result.detail = std::string("request failed: ") + httpErrorName(error);
          if (error == HttpError::BadStatus) {
            result.detail += " (HTTP " + std::to_string(response.status) + ")";
          }
          return result;
        }
        const std::optional<std::vector<ReleaseInfo>> releases =
            parseGitHubReleases(response.body);
        if (!releases) {
          result.kind = CheckResult::Kind::Failed;
          result.detail = "invalid response";
          return result;
        }
        if (const ReleaseInfo *selected = selectUpdate(*releases, options)) {
          result.kind = CheckResult::Kind::UpdateAvailable;
          result.release = *selected;
        } else {
          result.kind = CheckResult::Kind::UpToDate;
        }
        return result;
      },
      CheckResult{CheckResult::Kind::Failed, std::nullopt, HttpError::Other, 0, "internal error"});
}

bool isAutomaticCheckDue(const UpdateSettings &settings, std::uint64_t nowUnix) {
  if (!settings.autoCheck || nowUnix < settings.nextCheckNotBeforeUnix) {
    return false;
  }
  const bool clockMovedBack = settings.lastCheckUnix > nowUnix;
  return clockMovedBack || nowUnix - settings.lastCheckUnix >= kCheckIntervalSeconds;
}

void recordCheckOutcome(UpdateSettings &settings, const CheckResult &result,
                        std::uint64_t nowUnix) {
  switch (result.kind) {
  case CheckResult::Kind::UpToDate:
    settings.lastCheckUnix = nowUnix;
    settings.nextCheckNotBeforeUnix = nowUnix + kCheckIntervalSeconds;
    break;
  case CheckResult::Kind::UpdateAvailable:
    settings.lastCheckUnix = nowUnix;
    settings.nextCheckNotBeforeUnix = nowUnix;
    break;
  case CheckResult::Kind::Failed:
    settings.nextCheckNotBeforeUnix = nowUnix + kRetryAfterFailureSeconds;
    break;
  case CheckResult::Kind::RateLimited:
    settings.nextCheckNotBeforeUnix =
        std::max(result.retryAfterUnix, nowUnix + kMinRateLimitBackoffSeconds);
    break;
  }
}

// ----------------------------------------------------------------- staging

const char *stageErrorName(StageError error) {
  switch (error) {
  case StageError::None: return "none";
  case StageError::MissingAssets: return "missing-assets";
  case StageError::BadAssetUrl: return "bad-asset-url";
  case StageError::Network: return "network";
  case StageError::Cancelled: return "cancelled";
  case StageError::BadSignature: return "bad-signature";
  case StageError::Malformed: return "malformed";
  case StageError::VersionMismatch: return "version-mismatch";
  case StageError::NotNewer: return "not-newer";
  case StageError::TooLarge: return "too-large";
  case StageError::IntegrityFailed: return "integrity-failed";
  case StageError::ExtractFailed: return "extract-failed";
  case StageError::NoSpace: return "no-space";
  case StageError::Io: return "io";
  }
  return "unknown";
}

StageResult stageUpdate(IHttpClient &http, const ReleaseInfo &release, const Version &current,
                        std::span<const TrustedKey> keys, const std::wstring &stateDir,
                        const ProgressFn &progress) {
  return guarded([&] { return stageUpdateImpl(http, release, current, keys, stateDir, progress); },
                 stageFailure(StageError::Io, "internal error"));
}

std::optional<Manifest> loadStagedUpdate(const std::wstring &stateDir,
                                         std::span<const TrustedKey> keys) {
  return guarded(
      [&]() -> std::optional<Manifest> {
        if (stateDir.empty()) {
          return std::nullopt;
        }
        const std::wstring pending = joinPath(stateDir, kPendingDirName);
        if (!fileExists(joinPath(pending, kReadyMarkerName))) {
          return std::nullopt;
        }
        const std::optional<std::string> manifestBytes =
            readFile(joinPath(pending, kManifestFileName), kMaxManifestBytes);
        const std::optional<std::string> signature =
            readFile(joinPath(pending, kSignatureFileName), kSignatureBytes);
        if (!manifestBytes || !signature || signature->size() != kSignatureBytes) {
          return std::nullopt;
        }
        const std::optional<Manifest> manifest =
            verifyAndParseManifest(bytesOf(*manifestBytes), bytesOf(*signature), keys);
        if (!manifest) {
          return std::nullopt;
        }
        const std::wstring filesRoot = joinPath(pending, kFilesDirName);
        for (const ManifestFile &file : manifest->files) {
          std::wstring relative = utf8ToWide(file.path);
          std::replace(relative.begin(), relative.end(), L'/', L'\\');
          std::uint64_t size = 0;
          const std::optional<Sha256Digest> digest =
              sha256File(joinPath(filesRoot, relative), &size);
          if (!digest || size != file.size || *digest != file.sha256) {
            return std::nullopt;
          }
        }
        return manifest;
      },
      std::optional<Manifest>{});
}

} // namespace fsv::update
