// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

// Standard and updater-lib headers first (see UpdateDialog.cpp).
#include "updater-lib/FsUtil.h"
#include "updater-lib/Installer.h"
#include "updater-lib/UpdateService.h"

#include "UpdateController.h"
#include "UpdateDialog.h"
#include "TvnViewer.h"

#include <atomic>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace fsv::update;

namespace {

const wchar_t UPDATER_EXE_NAME[] = L"FarSightUpdater.exe";
const size_t MAX_NOTES_BYTES = 64 * 1024;
const DWORD PROGRESS_POST_INTERVAL_MS = 100;

std::wstring widen(const char *text)
{
  return utf8ToWide(text);
}

std::wstring versionText(const Version &version)
{
  return widen(version.toString().c_str());
}

// Release notes are Markdown with LF line breaks; the edit control needs CRLF.
std::wstring notesForEdit(const std::string &notes)
{
  std::string text = notes;
  bool truncated = false;
  if (text.size() > MAX_NOTES_BYTES) {
    size_t cut = MAX_NOTES_BYTES;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
      --cut; // do not split a UTF-8 sequence
    }
    text.resize(cut);
    truncated = true;
  }
  std::wstring wide = utf8ToWide(text);
  std::wstring result;
  result.reserve(wide.size() + wide.size() / 16);
  for (size_t i = 0; i < wide.size(); i++) {
    wchar_t c = wide[i];
    if (c == L'\r') {
      if (i + 1 < wide.size() && wide[i + 1] == L'\n') {
        i++;
      }
      result += L"\r\n";
    } else if (c == L'\n') {
      result += L"\r\n";
    } else {
      result += c;
    }
  }
  if (truncated) {
    result += L"\r\n\r\n[...]";
  }
  return result;
}

std::wstring releaseDate(const std::string &publishedAt)
{
  if (publishedAt.size() >= 10 && publishedAt[4] == '-' && publishedAt[7] == '-') {
    return utf8ToWide(publishedAt.substr(0, 10));
  }
  return std::wstring();
}

std::wstring networkReason(HttpError error)
{
  switch (error) {
  case HttpError::Offline:
    return L"FarSightVNC could not reach GitHub. Check your internet connection and try again.";
  case HttpError::Timeout:
    return L"The connection to GitHub timed out. Try again later.";
  case HttpError::Tls:
    return L"A secure connection to GitHub could not be established (certificate or TLS error).";
  default:
    return L"GitHub could not be contacted. Try again later.";
  }
}

std::wstring rateLimitReason(std::uint64_t retryAfterUnix)
{
  if (retryAfterUnix == 0) {
    return L"GitHub's request limit was reached; try again later.";
  }
  std::time_t when = static_cast<std::time_t>(retryAfterUnix);
  std::tm local = {};
  if (localtime_s(&local, &when) != 0) {
    return L"GitHub's request limit was reached; try again later.";
  }
  wchar_t text[96];
  swprintf_s(text, L"GitHub's request limit was reached; try again after %02d:%02d.",
             local.tm_hour, local.tm_min);
  return text;
}

std::wstring checkFailureText(const CheckResult &result)
{
  if (result.kind == CheckResult::Kind::RateLimited ||
      result.httpError == HttpError::RateLimited) {
    return rateLimitReason(result.retryAfterUnix);
  }
  return networkReason(result.httpError) + L"\r\n\r\nYou can also open the releases page.";
}

std::wstring stageFailureText(const StageResult &result)
{
  const wchar_t *hint = L"\r\n\r\nYou can download it from the releases page instead.";
  std::wstring reason;
  switch (result.error) {
  case StageError::MissingAssets:
  case StageError::BadAssetUrl:
    reason = L"This release cannot be installed automatically.";
    break;
  case StageError::Network:
    reason = L"The update could not be downloaded. " + networkReason(result.httpError);
    break;
  case StageError::BadSignature:
  case StageError::IntegrityFailed:
    reason = L"The downloaded update failed verification and was discarded.";
    break;
  case StageError::Malformed:
  case StageError::VersionMismatch:
  case StageError::NotNewer:
  case StageError::ExtractFailed:
    reason = L"The downloaded update is not valid and was discarded.";
    break;
  case StageError::TooLarge:
    reason = L"The update is larger than expected and was not downloaded.";
    break;
  case StageError::NoSpace:
    reason = L"There is not enough free disk space to prepare the update.";
    break;
  default:
    reason = L"The update could not be saved on this computer.";
    break;
  }
  return reason + hint;
}

// Quotes one command-line argument; false if it cannot be quoted safely.
bool quoteArgument(const std::wstring &value, std::wstring &out)
{
  if (value.empty() || value.find(L'"') != std::wstring::npos) {
    return false;
  }
  out = L"\"" + value;
  if (value.back() == L'\\') {
    out += L'\\'; // keep the closing quote from being escaped
  }
  out += L"\"";
  return true;
}

std::uint64_t unixNow()
{
  return static_cast<std::uint64_t>(std::time(nullptr));
}

} // namespace

struct UpdateController::Impl : public UpdateDialog::Host
{
  enum class Operation { None, Check, Stage };

  explicit Impl(HWND mainWindow)
  : window(mainWindow), dialog(this) {}

  ~Impl()
  {
    cancelWork();
    if (worker.joinable()) {
      worker.join();
    }
    dialog.close();
  }

  // ------------------------------------------------------- identity (lazy)

  void ensureInitialized()
  {
    if (initialized) {
      return;
    }
    initialized = true;
    std::wstring exe = currentModulePath();
    std::optional<Version> version = readExecutableVersion(exe);
    const char *arch = buildArchitecture();
    if (!version || arch == nullptr || arch[0] == '\0') {
      return;
    }
    current = *version;
    target = detectInstallTarget(exe, arch);
    stateDir = updateStateDirectory(target.root);
    userAgent = L"FarSightVNC-Updater/" + versionText(current);
    settings = store.load();
    available = true;
  }

  // ----------------------------------------------------------- UI helpers

  void presentDialog(UpdateDialog::State state, const std::wstring &text,
                     UpdateDialog::Focus focus)
  {
    dialog.setOptions(settings.autoCheck, effectiveIncludePrereleases(settings, current));
    dialog.present(state, text, focus);
  }

  std::wstring readyText() const
  {
    std::wstring text = L"FarSightVNC " + versionText(*stagedVersion) +
      L" has been downloaded and verified. It will be installed automatically "
      L"after you close all FarSightVNC windows. Active connections are not interrupted.";
    if (needsElevation) {
      text += L"\r\n\r\nInstalling will ask for administrator permission.";
    }
    return text;
  }

  void presentAvailable(UpdateDialog::Focus focus)
  {
    UpdateDialog::AvailableInfo info;
    info.currentVersion = versionText(current);
    info.availableVersion = versionText(release->version);
    info.releaseDate = releaseDate(release->publishedAt);
    info.channel = widen(channelName(release->version.channel));
    if (isPrereleaseRelease(*release)) {
      info.channel += L" (pre-release)";
    }
    info.notes = notesForEdit(release->notes);
    if (release->htmlUrl.rfind(kReleasesPageUrl, 0) == 0) {
      info.releaseUrl = utf8ToWide(release->htmlUrl);
    }
    info.canInstall = release->findAsset(kManifestAssetName) != nullptr &&
                      release->findAsset(kSignatureAssetName) != nullptr;
    if (info.canInstall && needsElevation) {
      info.note = L"Installing will ask for administrator permission.";
    }
    dialog.setAvailable(info);
    presentDialog(UpdateDialog::State::Available,
                  L"A new version of FarSightVNC is available.", focus);
  }

  // --------------------------------------------------------------- worker

  void cancelWork()
  {
    cancelRequested = true;
    std::lock_guard<std::mutex> lock(mutex);
    if (http) {
      http->cancel();
    }
  }

  void joinWorker()
  {
    if (worker.joinable()) {
      worker.join();
    }
    busy = false;
    operation = Operation::None;
  }

  std::shared_ptr<IHttpClient> openClient()
  {
    std::unique_ptr<IHttpClient> client = createWinHttpClient(userAgent);
    if (!client) {
      return nullptr;
    }
    std::shared_ptr<IHttpClient> shared(std::move(client));
    std::lock_guard<std::mutex> lock(mutex);
    http = shared;
    return shared;
  }

  void closeClient()
  {
    std::lock_guard<std::mutex> lock(mutex);
    http.reset();
  }

  bool startThread(Operation op, std::function<void()> body)
  {
    try {
      busy = true;
      operation = op;
      cancelRequested = false;
      worker = std::thread(std::move(body));
      return true;
    } catch (...) {
      busy = false;
      operation = Operation::None;
      return false;
    }
  }

  void startCheck(bool manual)
  {
    SelectionOptions options;
    options.current = current;
    options.includePrereleases = effectiveIncludePrereleases(settings, current);
    options.skipped = settings.skipped;
    options.manual = manual;

    bool started = startThread(Operation::Check, [this, options, manual]() {
      CheckResult result;
      std::shared_ptr<IHttpClient> client = openClient();
      if (!client) {
        result.kind = CheckResult::Kind::Failed;
        result.httpError = HttpError::Other;
        result.detail = "HTTP unavailable";
      } else if (cancelRequested) {
        result.kind = CheckResult::Kind::Failed;
        result.httpError = HttpError::Cancelled;
      } else {
        result = checkForUpdate(*client, options);
      }
      closeClient();
      {
        std::lock_guard<std::mutex> lock(mutex);
        checkResult = std::move(result);
        checkManual = manual;
      }
      PostMessage(window, TvnViewer::WM_USER_UPDATE_CHECK_DONE, 0, 0);
    });
    if (!started && manual) {
      presentDialog(UpdateDialog::State::Failed,
                    L"The update check could not be started.\r\n\r\n"
                    L"You can also open the releases page.",
                    UpdateDialog::Focus::Activate);
    }
  }

  void startStage()
  {
    ReleaseInfo target_release = *release;
    Version running = current;
    std::wstring state = stateDir;

    progressReceived = 0;
    progressTotal = 0;
    lastProgressPost = GetTickCount() - PROGRESS_POST_INTERVAL_MS;

    bool started = startThread(Operation::Stage, [this, target_release, running, state]() {
      StageResult result;
      std::shared_ptr<IHttpClient> client = openClient();
      if (!client) {
        result.error = StageError::Network;
        result.httpError = HttpError::Other;
        result.detail = "HTTP unavailable";
      } else if (cancelRequested) {
        result.error = StageError::Cancelled;
      } else {
        ProgressFn progress = [this](std::uint64_t received, std::uint64_t total) {
          progressReceived = received;
          progressTotal = total;
          DWORD now = GetTickCount();
          if (now - lastProgressPost >= PROGRESS_POST_INTERVAL_MS) {
            lastProgressPost = now;
            PostMessage(window, TvnViewer::WM_USER_UPDATE_PROGRESS, 0, 0);
          }
          return !cancelRequested.load();
        };
        result = stageUpdate(*client, target_release, running, productionKeys(), state, progress);
      }
      closeClient();
      {
        std::lock_guard<std::mutex> lock(mutex);
        stageResult = std::move(result);
      }
      PostMessage(window, TvnViewer::WM_USER_UPDATE_STAGE_DONE, 0, 0);
    });
    if (!started) {
      presentDialog(UpdateDialog::State::StageFailed,
                    L"The download could not be started.\r\n\r\n"
                    L"You can download it from the releases page instead.",
                    UpdateDialog::Focus::Keep);
    }
  }

  // ------------------------------------------------- UpdateDialog::Host

  void onUpdateDownloadRequested()
  {
    if (busy || !release || !available) {
      return;
    }
    needsElevation = !canWriteDirectory(target.root);
    dialog.setProgress(0, 0);
    presentDialog(UpdateDialog::State::Downloading,
                  L"Downloading FarSightVNC " + versionText(release->version) + L"...",
                  UpdateDialog::Focus::Keep);
    startStage();
  }

  void onUpdateCancelDownload()
  {
    if (busy && operation == Operation::Stage) {
      cancelWork();
    }
  }

  void onUpdateCancelStaged()
  {
    if (!available) {
      return;
    }
    removeTree(joinPath(stateDir, kPendingDirName), stateDir);
    stagedVersion.reset();
  }

  void onUpdateSkipRequested()
  {
    if (!release) {
      return;
    }
    settings.skipped = release->version;
    store.save(settings);
  }

  void onUpdateAutoCheckToggled(bool enabled)
  {
    settings.autoCheck = enabled;
    store.save(settings);
  }

  void onUpdatePrereleaseToggled(bool enabled)
  {
    settings.includePrereleases = enabled;
    store.save(settings);
  }

  // ---------------------------------------------------------------- state

  HWND window;
  UpdateDialog dialog;

  bool initialized = false;
  bool available = false;       // false: the running build cannot be updated
  Version current;
  InstallTarget target;
  std::wstring stateDir;
  std::wstring userAgent;

  UpdateSettingsStore store;
  UpdateSettings settings;

  std::thread worker;
  std::atomic<bool> busy{false};
  std::atomic<bool> cancelRequested{false};
  Operation operation = Operation::None;

  std::mutex mutex;             // guards http, checkResult, checkManual, stageResult
  std::shared_ptr<IHttpClient> http;
  CheckResult checkResult;
  bool checkManual = false;
  StageResult stageResult;

  std::atomic<std::uint64_t> progressReceived{0};
  std::atomic<std::uint64_t> progressTotal{0};
  std::atomic<DWORD> lastProgressPost{0};

  std::optional<ReleaseInfo> release;     // the release offered in the dialog
  std::optional<Version> stagedVersion;   // downloaded and verified in this session
  bool needsElevation = false;
};

UpdateController::UpdateController(HWND mainWindow)
: m_impl(new Impl(mainWindow))
{
}

UpdateController::~UpdateController()
{
}

bool UpdateController::interruptedUpdateFound()
{
  m_impl->ensureInitialized();
  return m_impl->available && hasPendingJournal(m_impl->target.root);
}

void UpdateController::onStartupTimer()
{
  Impl &d = *m_impl;
  d.ensureInitialized();
  if (!d.available || d.busy || updatesDisabledByPolicy()) {
    return;
  }
  d.settings = d.store.load();
  if (!isAutomaticCheckDue(d.settings, unixNow())) {
    return;
  }
  d.startCheck(false);
}

void UpdateController::requestManualCheck()
{
  Impl &d = *m_impl;
  d.ensureInitialized();
  if (!d.available) {
    MessageBoxW(0, L"Update checks are not available in this build of FarSightVNC.",
                L"FarSightVNC", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (updatesDisabledByPolicy()) {
    d.presentDialog(UpdateDialog::State::Disabled,
                    L"Update checks are disabled by your administrator.",
                    UpdateDialog::Focus::Activate);
    return;
  }
  if (d.busy) {
    // An operation is running: just bring its dialog forward.
    d.dialog.reopen(UpdateDialog::Focus::Activate);
    return;
  }
  if (d.stagedVersion) {
    d.presentDialog(UpdateDialog::State::Ready, d.readyText(), UpdateDialog::Focus::Activate);
    return;
  }
  d.settings = d.store.load();
  d.presentDialog(UpdateDialog::State::Checking, L"Checking for updates...",
                  UpdateDialog::Focus::Activate);
  d.startCheck(true);
}

void UpdateController::onCheckDone()
{
  Impl &d = *m_impl;
  d.joinWorker();

  CheckResult result;
  bool manual = false;
  {
    std::lock_guard<std::mutex> lock(d.mutex);
    result = std::move(d.checkResult);
    manual = d.checkManual;
  }
  if (result.httpError == HttpError::Cancelled) {
    return;
  }

  recordCheckOutcome(d.settings, result, unixNow());
  d.store.save(d.settings);

  using Focus = UpdateDialog::Focus;
  switch (result.kind) {
  case CheckResult::Kind::UpdateAvailable:
    if (!result.release) {
      break;
    }
    d.release = std::move(result.release);
    d.needsElevation = !canWriteDirectory(d.target.root);
    if (d.stagedVersion && *d.stagedVersion == d.release->version) {
      d.presentDialog(UpdateDialog::State::Ready, d.readyText(),
                      manual ? Focus::Activate : Focus::NoActivate);
    } else {
      d.presentAvailable(manual ? Focus::Activate : Focus::NoActivate);
    }
    break;

  case CheckResult::Kind::UpToDate:
    if (manual) {
      d.presentDialog(UpdateDialog::State::UpToDate,
                      L"FarSightVNC " + versionText(d.current) +
                      L" is the latest version for the " +
                      widen(channelName(d.current.channel)) + L" channel.",
                      Focus::Activate);
    }
    break;

  case CheckResult::Kind::Failed:
  case CheckResult::Kind::RateLimited:
    // Automatic failures are never shown.
    if (manual) {
      d.presentDialog(UpdateDialog::State::Failed, checkFailureText(result), Focus::Activate);
    }
    break;
  }
}

void UpdateController::onProgress()
{
  Impl &d = *m_impl;
  if (d.busy && d.operation == Impl::Operation::Stage) {
    d.dialog.setProgress(d.progressReceived, d.progressTotal);
  }
}

void UpdateController::onStageDone()
{
  Impl &d = *m_impl;
  d.joinWorker();

  StageResult result;
  {
    std::lock_guard<std::mutex> lock(d.mutex);
    result = std::move(d.stageResult);
  }

  using Focus = UpdateDialog::Focus;
  if (result.error == StageError::None && d.release) {
    d.stagedVersion = d.release->version;
    d.presentDialog(UpdateDialog::State::Ready, d.readyText(), Focus::Keep);
  } else if (result.error == StageError::Cancelled || result.httpError == HttpError::Cancelled) {
    if (d.release) {
      d.presentAvailable(Focus::Keep);
    }
  } else {
    d.presentDialog(UpdateDialog::State::StageFailed, stageFailureText(result), Focus::Keep);
  }
}

// ------------------------------------------------------------------ exit hook

void launchPendingUpdaterAtExit()
{
  try {
    if (updatesDisabledByPolicy()) {
      return;
    }
    const char *arch = buildArchitecture();
    if (arch == nullptr || arch[0] == '\0') {
      return;
    }
    std::wstring viewerPath = currentModulePath();
    InstallTarget target = detectInstallTarget(viewerPath, arch);
    std::wstring stateDir = updateStateDirectory(target.root);
    std::wstring pending = joinPath(stateDir, kPendingDirName);

    const wchar_t *mode = nullptr;
    if (hasPendingJournal(target.root)) {
      mode = L"--recover";
    } else if (fileExists(joinPath(pending, kReadyMarkerName))) {
      mode = L"--install";
    }
    if (mode == nullptr) {
      return;
    }
    bool install = (std::wstring(mode) == L"--install");

    // Run the updater from the state directory: it replaces the viewer folder.
    std::wstring binDir = joinPath(stateDir, L"bin");
    std::wstring binExe = joinPath(binDir, UPDATER_EXE_NAME);
    std::wstring source = joinPath(parentDirectory(viewerPath), UPDATER_EXE_NAME);
    if (!fileExists(source) && install) {
      std::wstring stagedArch = utf8ToWide(arch);
      source = joinPath(joinPath(joinPath(pending, L"files"), stagedArch), UPDATER_EXE_NAME);
    }
    if (!ensureDirectory(binDir)) {
      return;
    }
    if (fileExists(source)) {
      if (!CopyFileW(source.c_str(), binExe.c_str(), FALSE)) {
        return;
      }
    } else if (!fileExists(binExe)) {
      return;
    }

    std::wstring qExe, qState, qViewer;
    if (!quoteArgument(binExe, qExe) || !quoteArgument(stateDir, qState) ||
        !quoteArgument(viewerPath, qViewer)) {
      return;
    }
    std::wstring commandLine = qExe + L" --protocol 1 " + mode + L" --state " + qState +
                               L" --viewer " + qViewer + L" --arch " + utf8ToWide(arch) +
                               L" --parent-pid " + std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    DWORD flags = CREATE_BREAKAWAY_FROM_JOB | CREATE_NO_WINDOW;
    BOOL created = CreateProcessW(binExe.c_str(), buffer.data(), 0, 0, FALSE, flags, 0,
                                  binDir.c_str(), &startup, &process);
    if (!created && GetLastError() == ERROR_ACCESS_DENIED) {
      // The job does not allow breakaway: start inside it.
      flags = CREATE_NO_WINDOW;
      created = CreateProcessW(binExe.c_str(), buffer.data(), 0, 0, FALSE, flags, 0,
                               binDir.c_str(), &startup, &process);
    }
    if (created) {
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
  } catch (...) {
    // Exiting: nothing can be reported, and the update stays staged.
  }
}
