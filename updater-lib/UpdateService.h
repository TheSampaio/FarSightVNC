// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Viewer-side update operations: check (GitHub API), preferences and check
// policy, and staging (download + verify + extract). Runs unelevated; nothing
// here executes downloaded content.

#pragma once

#include "HttpClient.h"
#include "Manifest.h"
#include "Releases.h"

#include <functional>

namespace fsv::update {

// ---------------------------------------------------------------- settings

struct UpdateSettings {
  bool autoCheck = true;
  std::optional<bool> includePrereleases; // unset -> current.isPrerelease()
  std::optional<Version> skipped;
  std::uint64_t lastCheckUnix = 0;
  std::uint64_t nextCheckNotBeforeUnix = 0;
  // The last release found by a check, cached as GitHub-style JSON
  // (serializeReleaseForCache), so that later launches offer it again
  // without a request. Empty: nothing pending.
  std::string pendingRelease;
};

// Where the settings live. Production uses the registry (UpdateSettingsStore);
// tests substitute an in-memory store (see runCheck()).
class IUpdateSettingsStore {
public:
  virtual ~IUpdateSettingsStore() = default;
  virtual UpdateSettings load() const = 0; // missing/invalid values -> defaults
  virtual bool save(const UpdateSettings &settings) const = 0;
};

// HKCU\Software\FarSightVNC\Viewer\Updater (values AutoCheck, IncludePrereleases,
// SkippedVersion, LastCheck, NextCheckNotBefore, PendingRelease as REG_BINARY
// UTF-8 of at most kMaxPendingReleaseBytes). Tests pass another subkey.
class UpdateSettingsStore final : public IUpdateSettingsStore {
public:
  explicit UpdateSettingsStore(std::wstring subKey = L"Software\\FarSightVNC\\Viewer\\Updater");
  UpdateSettings load() const override;
  bool save(const UpdateSettings &settings) const override;

private:
  std::wstring m_subKey;
};

// HKLM\SOFTWARE\Policies\FarSightVNC\Viewer\Updater, DWORD DisableUpdateCheck=1
// disables automatic and manual checks (administrator policy).
bool updatesDisabledByPolicy();

bool effectiveIncludePrereleases(const UpdateSettings &settings, const Version &current);

// ------------------------------------------------------------------- check

struct CheckResult {
  enum class Kind { UpdateAvailable, UpToDate, Failed, RateLimited };
  Kind kind = Kind::Failed;
  std::optional<ReleaseInfo> release; // UpdateAvailable only
  HttpError httpError = HttpError::None;
  std::uint64_t retryAfterUnix = 0;   // RateLimited only
  std::string detail;                 // short English diagnostic
};

// GET kReleasesApiUrl (Accept: application/vnd.github+json, max 2 MiB),
// parse, selectUpdate(). Never throws.
CheckResult checkForUpdate(IHttpClient &http, const SelectionOptions &options);

inline constexpr std::uint64_t kCheckIntervalSeconds = 24 * 3600;
inline constexpr std::uint64_t kRetryAfterFailureSeconds = 6 * 3600;
inline constexpr std::uint64_t kMinRateLimitBackoffSeconds = 3600;

// Upper bound for a legitimate schedule (rate-limit resets are ~1 h; the
// longest outcome is the 24 h interval).
inline constexpr std::uint64_t kMaxScheduleAheadSeconds = 7 * 24 * 3600;

// autoCheck && (now >= nextCheckNotBefore
//               || nextCheckNotBefore > now + kMaxScheduleAheadSeconds /*clock moved back*/).
// lastCheck is informational only.
bool isAutomaticCheckDue(const UpdateSettings &settings, std::uint64_t nowUnix);

// UpToDate: lastCheck = now, next = now + interval, pendingRelease cleared.
// UpdateAvailable: lastCheck = now, next = now + interval, pendingRelease =
//   the release: later launches offer it again from the cache, without a
//   request, until it is installed or skipped ("Remind Me Later" persists
//   nothing else).
// Failed: next = now + retry. RateLimited: next = max(reset, now + min backoff).
// Both keep pendingRelease.
void recordCheckOutcome(UpdateSettings &settings, const CheckResult &result,
                        std::uint64_t nowUnix);

inline constexpr std::size_t kMaxPendingReleaseBytes = 128 * 1024;
inline constexpr std::size_t kMaxCachedNotesBytes = 64 * 1024;

// The release as a one-element GitHub releases array (parseGitHubReleases
// reads it back). Notes longer than kMaxCachedNotesBytes are cut at a UTF-8
// boundary.
std::string serializeReleaseForCache(const ReleaseInfo &release);

// The cached release if an automatic check would still offer it (newer than
// `current`, channel allowed, not skipped); nullopt otherwise.
std::optional<ReleaseInfo> cachedAutomaticOffer(const UpdateSettings &settings,
                                                const Version &current);

// ------------------------------------------------------ guarded check run

// Non-blocking lock that keeps two viewer processes of the same user session
// from checking at the same time. Production: NamedMutexCheckLock.
class ICheckLock {
public:
  virtual ~ICheckLock() = default;
  virtual bool tryAcquire() = 0; // false: another process holds it, or lastError()
  virtual void release() = 0;    // only after a successful tryAcquire()
  // Win32 error of the last failed tryAcquire() that was not contention (0: none).
  virtual unsigned long lastError() const { return 0; }
};

inline constexpr wchar_t kAutomaticCheckMutexName[] = L"Local\\FarSightVNC-UpdateCheck";

// A Win32 named mutex. Ownership is per thread: tryAcquire() and release()
// must run on the same thread (the viewer's worker thread does both).
class NamedMutexCheckLock final : public ICheckLock {
public:
  explicit NamedMutexCheckLock(std::wstring name = kAutomaticCheckMutexName);
  ~NamedMutexCheckLock() override;
  NamedMutexCheckLock(const NamedMutexCheckLock &) = delete;
  NamedMutexCheckLock &operator=(const NamedMutexCheckLock &) = delete;
  bool tryAcquire() override;
  void release() override;
  unsigned long lastError() const override { return m_error; }

private:
  std::wstring m_name;
  void *m_handle = nullptr;
  bool m_owned = false;
  unsigned long m_error = 0;
};

using UnixClock = std::function<std::uint64_t()>;

enum class CheckTrigger { Automatic, Manual };

struct CheckRun {
  enum class Status {
    Checked,    // the request ran; result holds the outcome
    NotDue,     // automatic only: the policy says it is not time yet
    Busy,       // automatic only: another process is checking right now
    Cached,     // automatic only: not due, the pending release is offered again
                //   (result.kind = UpdateAvailable, no request)
    LockFailed, // automatic only: the named mutex could not be created/waited
                //   (result.detail has the Win32 error); nothing happened
  };
  Status status = Status::NotDue;
  CheckResult result;      // Checked only
  UpdateSettings settings; // the stored settings after the run
};

// One update check with the persistence around it. Never throws.
// Automatic: tryAcquire `lock` (held -> Busy, nothing else happens), re-read the
//   settings from `store` and re-evaluate isAutomaticCheckDue(clock()) (not due ->
//   NotDue: this keeps several processes to one check per interval; a pending
//   release that is still eligible is offered from the cache instead -> Cached,
//   one that is not any more is dropped), check with the skipped version
//   honoured, record the outcome, then release the lock.
// Manual: no lock and no due test; the skipped version is ignored.
// Recording re-reads the store right before saving, so only the schedule
// (LastCheck/NextCheckNotBefore) changes; a cancelled request records nothing.
CheckRun runCheck(CheckTrigger trigger, IHttpClient &http, const IUpdateSettingsStore &store,
                  ICheckLock &lock, const Version &current, const UnixClock &clock);

// ----------------------------------------------------------------- staging

// Layout of a staged update (inside updateStateDirectory(root)\pending):
//   farsightvnc-update.manifest, farsightvnc-update.manifest.sig,
//   package.zip, files\<manifest paths>, and "ready" written last.
inline constexpr wchar_t kPendingDirName[] = L"pending";
inline constexpr wchar_t kReadyMarkerName[] = L"ready";

enum class StageError {
  None,
  MissingAssets,   // release lacks manifest/signature/package asset
  BadAssetUrl,     // asset URL outside kAssetUrlPrefix
  Network,
  Cancelled,
  BadSignature,
  Malformed,
  VersionMismatch, // manifest version != release tag version
  NotNewer,        // manifest version <= current (downgrade/replay)
  TooLarge,
  IntegrityFailed, // package size/SHA-256 mismatch
  ExtractFailed,
  NoSpace,
  Io,
};

const char *stageErrorName(StageError error);

struct StageResult {
  StageError error = StageError::Io;
  HttpError httpError = HttpError::None;
  std::string detail;
  std::optional<Manifest> manifest;
};

// Downloads manifest + signature, verifies them against `keys`, checks version
// == release.version and > current, downloads the package (size bounded by the
// manifest, SHA-256 checked), extracts the listed files and writes the ready
// marker. pendingDir is wiped (removeTree guarded by stateDir) and recreated.
StageResult stageUpdate(IHttpClient &http, const ReleaseInfo &release,
                        const Version &current, std::span<const TrustedKey> keys,
                        const std::wstring &stateDir, const ProgressFn &progress);

// Reads and re-verifies a staged update (signature + every file hash).
// Returns the manifest if `stateDir\pending` is complete and valid.
std::optional<Manifest> loadStagedUpdate(const std::wstring &stateDir,
                                         std::span<const TrustedKey> keys);

// ---------------------------------------------------------- install result

// fsvncupdater writes one line into stateDir\last-result.txt after an install:
// "ok <version>", "failed <install error>" or "declined <version|unknown>".
// The viewer renames it to last-result.ack once it has told the user.
inline constexpr wchar_t kLastResultFileName[] = L"last-result.txt";
inline constexpr wchar_t kLastResultAckFileName[] = L"last-result.ack";

struct InstallOutcome {
  enum class Kind { Installed, Failed, Declined };
  Kind kind = Kind::Failed;
  std::optional<Version> version; // Installed (always), Declined (when known)
  std::string detail;             // Failed: the install error name
};

// Parses the line (surrounding whitespace ignored); nullopt if malformed.
std::optional<InstallOutcome> parseInstallOutcome(std::string_view text);

// True, once: if stateDir\last-result.txt records a successful install of
// exactly `running`, it is renamed to last-result.ack (replacing an older
// one) so the "updated" notice is shown a single time. Anything else (no
// file, another version, a failure) returns false and leaves the file alone.
bool acknowledgeInstalledUpdate(const std::wstring &stateDir, const Version &running);

} // namespace fsv::update
