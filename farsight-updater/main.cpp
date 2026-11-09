// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// FarSightUpdater.exe: waits for the viewer processes of one installation to
// exit, then installs a staged, signed update or repairs an interrupted
// install (see docs/updater/DESIGN.md, "Flow" steps 5 and 6). Protocol 1:
//
//   --protocol 1 --install|--recover --state "<stateDir>" --viewer "<tvnviewer.exe>"
//   --arch x64|Win32 [--parent-pid N] [--elevated]
//
// No network calls; the only windows are message boxes.

#include <windows.h>

#include <shellapi.h>

#include "updater-lib/Crypto.h"
#include "updater-lib/FsUtil.h"
#include "updater-lib/Installer.h"
#include "updater-lib/UpdateService.h"
#include "updater-lib/Win32Util.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

using namespace fsv::update;

namespace {

// Exit codes of FarSightUpdater.exe.
constexpr int kExitOk = 0;
constexpr int kExitNoUpdate = 3;   // nothing staged / nothing installable
constexpr int kExitNeedsAdmin = 4; // administrator permission was declined
constexpr int kExitFailed = 5;     // install or recovery failed
constexpr int kExitUsage = 64;     // unknown or missing arguments
constexpr int kExitRejected = 65;  // arguments failed validation
constexpr int kExitInternal = 70;  // unexpected exception

constexpr std::uint64_t kMaxLogBytes = 256 * 1024;
constexpr int kMaxInUseRetries = 3;
constexpr wchar_t kMutexPrefix[] = L"Local\\FarSightVNC-Updater-";
constexpr wchar_t kAppTitle[] = L"FarSightVNC Updater";

// ----------------------------------------------------------------- options

enum class Mode { Install, Recover };

struct Options {
  Mode mode = Mode::Install;
  std::wstring stateDir;
  std::wstring viewer;
  std::wstring arch;
  DWORD parentPid = 0;
  bool elevated = false;
};

bool parsePid(const std::wstring &text, DWORD &pid) {
  if (text.empty() || text.size() > 10) {
    return false;
  }
  wchar_t *end = nullptr;
  const unsigned long value = std::wcstoul(text.c_str(), &end, 10);
  if (end == nullptr || *end != L'\0' || value == 0) {
    return false;
  }
  pid = static_cast<DWORD>(value);
  return true;
}

// Strict parse; any unknown or missing argument returns nullopt.
std::optional<Options> parseArguments(const std::vector<std::wstring> &args) {
  Options options;
  bool protocolOk = false;
  bool haveMode = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::wstring &arg = args[i];
    const bool hasValue = i + 1 < args.size();
    if (arg == L"--install" || arg == L"--recover") {
      options.mode = arg == L"--install" ? Mode::Install : Mode::Recover;
      haveMode = true;
    } else if (arg == L"--elevated") {
      options.elevated = true;
    } else if (arg == L"--protocol" && hasValue) {
      protocolOk = args[++i] == L"1";
    } else if (arg == L"--state" && hasValue) {
      options.stateDir = args[++i];
    } else if (arg == L"--viewer" && hasValue) {
      options.viewer = args[++i];
    } else if (arg == L"--arch" && hasValue) {
      options.arch = args[++i];
    } else if (arg == L"--parent-pid" && hasValue) {
      if (!parsePid(args[++i], options.parentPid)) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
  if (!protocolOk || !haveMode || options.stateDir.empty() || options.viewer.empty() ||
      options.arch.empty()) {
    return std::nullopt;
  }
  return options;
}

std::vector<std::wstring> commandLineArguments() {
  std::vector<std::wstring> args;
  int count = 0;
  LPWSTR *argv = ::CommandLineToArgvW(::GetCommandLineW(), &count);
  if (argv == nullptr) {
    return args;
  }
  for (int i = 1; i < count; ++i) {
    args.emplace_back(argv[i]);
  }
  ::LocalFree(argv);
  return args;
}

// Quotes one argument for a command line (CommandLineToArgvW rules).
std::wstring quoteArgument(const std::wstring &arg) {
  std::wstring quoted = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    quoted.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    backslashes = 0;
    quoted += c;
  }
  quoted.append(backslashes * 2, L'\\');
  quoted += L'"';
  return quoted;
}

// ------------------------------------------------------------------- state

struct Context {
  Options options;
  InstallTarget target;
  std::wstring pendingDir;
  std::wstring lastResultPath;
};

bool isAbsolutePath(const std::wstring &path) {
  const bool drive = path.size() >= 3 && path[1] == L':' && path[2] == L'\\' &&
                     ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'));
  return drive || path.rfind(L"\\\\", 0) == 0;
}

std::wstring updatesBaseDirectory() {
  const std::wstring localAppData = localAppDataDirectory();
  if (localAppData.empty()) {
    return {};
  }
  return joinPath(joinPath(localAppData, L"FarSightVNC"), L"Updates");
}

// --arch, --viewer and --state must be mutually consistent and stay inside
// the updater's own state area; nothing else is trusted from the command line.
bool validate(const Options &options, InstallTarget &target) {
  if (options.arch.empty() || options.arch != utf8ToWide(buildArchitecture()) ||
      !isAbsolutePath(options.viewer)) {
    return false;
  }
  target = detectInstallTarget(options.viewer, buildArchitecture());
  const std::wstring expected = updateStateDirectory(target.root);
  if (target.root.empty() || expected.empty() || !isAbsolutePath(options.stateDir)) {
    return false;
  }
  if (options.elevated) {
    // With over-the-shoulder UAC (a standard user approving with another
    // administrator's credentials) the elevated child runs in a different
    // profile, so its own %LOCALAPPDATA% is not the parent's. Accept the
    // parent's state directory by shape: ...\FarSightVNC\Updates\<hash of root>.
    // Trust does not depend on this location: every staged byte is checked
    // against the signed manifest after being copied into the install folder.
    const std::wstring state = fullPath(options.stateDir);
    const std::wstring updates = parentDirectory(state);
    return _wcsicmp(fileName(state).c_str(), fileName(expected).c_str()) == 0 &&
           _wcsicmp(fileName(updates).c_str(), L"Updates") == 0 &&
           _wcsicmp(fileName(parentDirectory(updates)).c_str(), L"FarSightVNC") == 0;
  }
  const std::wstring base = updatesBaseDirectory();
  return !base.empty() && pathsEqual(options.stateDir, expected) &&
         pathsEqual(parentDirectory(fullPath(options.stateDir)), base);
}

// ---------------------------------------------------------------------- log

class Log {
public:
  explicit Log(const std::wstring &path) : m_path(path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
      const std::uint64_t size =
          (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
      if (size > kMaxLogBytes) {
        ::DeleteFileW(path.c_str());
      }
    }
  }

  // Paths, versions and error names only.
  void write(const std::string &text) const {
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    char stamp[48] = {};
    std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth,
                  now.wDay, now.wHour, now.wMinute, now.wSecond);
    const std::string line = std::string(stamp) + text + "\r\n";
    UniqueHandle file(::CreateFileW(m_path.c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.valid()) {
      DWORD written = 0;
      ::WriteFile(file.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
  }

  void write(const std::string &text, const std::wstring &path) const {
    write(text + " " + wideToUtf8(path));
  }

private:
  std::wstring m_path;
};

// ---------------------------------------------------------------- user text

// Message box with an offer to open the releases page.
void showMessageAndOfferReleases(const std::wstring &text, UINT icon) {
  const int answer =
      ::MessageBoxW(nullptr, text.c_str(), kAppTitle, icon | MB_YESNO | MB_SETFOREGROUND);
  if (answer == IDYES) {
    ::ShellExecuteA(nullptr, "open", kReleasesPageUrl, nullptr, nullptr, SW_SHOWNORMAL);
  }
}

// -------------------------------------------------------------- processes

void waitForParent(DWORD pid) {
  if (pid == 0) {
    return;
  }
  UniqueHandle parent(::OpenProcess(SYNCHRONIZE, FALSE, pid));
  if (parent.valid()) {
    ::WaitForSingleObject(parent.get(), INFINITE);
  }
}

void waitForViewers(const std::vector<std::wstring> &viewers) {
  waitForProcessesToExit(viewers, [] { return false; });
}

// Every tvnviewer.exe a recovery could be touching.
std::vector<std::wstring> recoveryViewers(const Context &context) {
  std::vector<std::wstring> viewers{context.options.viewer};
  if (context.target.layout == InstallLayout::Full) {
    viewers.push_back(joinPath(context.target.root, L"x64\\tvnviewer.exe"));
    viewers.push_back(joinPath(context.target.root, L"Win32\\tvnviewer.exe"));
  }
  return viewers;
}

// ------------------------------------------------------------- elevation

// Relaunches this exe with the same arguments plus --elevated. Returns the
// child's exit code, or nullopt if elevation did not happen (declined/failed).
std::optional<int> runElevated(const Context &context, const Log &log) {
  const Options &options = context.options;
  std::wstring parameters = L"--protocol 1 ";
  parameters += options.mode == Mode::Install ? L"--install" : L"--recover";
  parameters += L" --state " + quoteArgument(options.stateDir);
  parameters += L" --viewer " + quoteArgument(options.viewer);
  parameters += L" --arch " + quoteArgument(options.arch);
  if (options.parentPid != 0) {
    parameters += L" --parent-pid " + std::to_wstring(options.parentPid);
  }
  parameters += L" --elevated";

  const std::wstring self = currentModulePath();
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  info.lpVerb = L"runas";
  info.lpFile = self.c_str();
  info.lpParameters = parameters.c_str();
  info.nShow = SW_HIDE;
  if (!::ShellExecuteExW(&info) || info.hProcess == nullptr) {
    log.write("elevation was not granted (Win32 error " + std::to_string(::GetLastError()) + ")");
    return std::nullopt;
  }
  UniqueHandle child(info.hProcess);
  ::WaitForSingleObject(child.get(), INFINITE);
  DWORD exitCode = kExitFailed;
  ::GetExitCodeProcess(child.get(), &exitCode);
  log.write("elevated updater exited with code " + std::to_string(exitCode));
  return static_cast<int>(exitCode);
}

void writeLastResult(const Context &context, const std::string &text) {
  writeFileAtomic(context.lastResultPath, text);
}

// Administrator permission was declined (or elevation failed): the staged
// update is discarded so the UAC prompt does not come back on every exit.
int handleElevationDeclined(const Context &context, const Log &log,
                            const std::optional<Version> &version) {
  log.write("administrator permission not granted");
  if (context.options.mode == Mode::Install) {
    removeTree(context.pendingDir, context.options.stateDir);
    writeLastResult(context, "declined " + (version ? version->toString() : std::string("unknown")));
    showMessageAndOfferReleases(
        L"FarSightVNC update needs administrator permission to write to " + context.target.root +
            L". The update was not installed; your current version is unchanged. FarSightVNC "
            L"will offer it again on a later check. Open the releases page now?",
        MB_ICONINFORMATION);
  } else {
    showMessageAndOfferReleases(
        L"FarSightVNC needs administrator permission to repair an interrupted update in " +
            context.target.root +
            L". The repair will be retried the next time FarSightVNC closes. Open the releases "
            L"page now?",
        MB_ICONWARNING);
  }
  return kExitNeedsAdmin;
}

bool needsElevation(const Context &context, const std::vector<PlannedFile> &plan) {
  if (!canWriteDirectory(context.target.root)) {
    return true;
  }
  for (const PlannedFile &file : plan) {
    if (fileExists(file.target) && !canReplaceFile(file.target)) {
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------ install

int reportInstallFailure(const Context &context, const Log &log, InstallError error,
                         const std::string &detail, const Version &version) {
  log.write(std::string("install failed: ") + installErrorName(error) + " (" + detail + ")");
  writeLastResult(context, std::string("failed ") + installErrorName(error));
  std::wstring outcome = L"Your current version is unchanged. ";
  if (error == InstallError::RolledBack) {
    outcome = L"The previous version was restored. ";
  } else if (error == InstallError::RollbackFailed) {
    outcome = L"The previous version could not be fully restored; the repair will be retried "
              L"the next time FarSightVNC closes. ";
  }
  showMessageAndOfferReleases(L"FarSightVNC could not install version " +
                                  utf8ToWide(version.toString()) + L". " + outcome +
                                  L"Open the releases page to update manually?",
                              MB_ICONWARNING);
  return kExitFailed;
}

// applyInstall with the "viewer restarted meanwhile" retry.
InstallError applyWithRetries(const Context &context, const Log &log,
                              const std::vector<PlannedFile> &plan, std::string &detail) {
  InstallError error = InstallError::Io;
  for (int attempt = 0; attempt <= kMaxInUseRetries; ++attempt) {
    error = applyInstall(context.target, plan, &detail, nullptr);
    if (error != InstallError::InUse || attempt == kMaxInUseRetries) {
      break;
    }
    log.write("a target is in use again (" + detail + "); waiting");
    waitForViewers(viewerExecutables(plan));
  }
  return error;
}

int installPlan(const Context &context, const Log &log, const std::vector<PlannedFile> &plan,
                const Version &version) {
  std::string detail;
  if (hasPendingJournal(context.target.root)) {
    log.write("recovering an interrupted install first");
    const InstallError recovered = recoverInstall(context.target.root, &detail);
    if (recovered != InstallError::None) {
      return reportInstallFailure(context, log, recovered, detail, version);
    }
  }
  log.write("installing " + version.toString() + " into", context.target.root);
  const InstallError error = applyWithRetries(context, log, plan, detail);
  if (error != InstallError::None) {
    return reportInstallFailure(context, log, error, detail, version);
  }
  removeTree(context.pendingDir, context.options.stateDir);
  writeLastResult(context, "ok " + version.toString());
  log.write("installed version " + version.toString());
  return kExitOk;
}

int runInstall(const Context &context, const Log &log) {
  const std::optional<Manifest> manifest =
      loadStagedUpdate(context.options.stateDir, productionKeys());
  if (!manifest) {
    log.write("no valid staged update");
    return kExitNoUpdate;
  }
  const Version version = manifest->version;
  const std::optional<Version> installed = readExecutableVersion(context.options.viewer);
  if (installed && version <= *installed) {
    log.write("staged " + version.toString() + " is not newer than " + installed->toString());
    removeTree(context.pendingDir, context.options.stateDir);
    return kExitOk;
  }

  const std::vector<PlannedFile> plan =
      planInstall(*manifest, joinPath(context.pendingDir, L"files"), context.target);
  if (plan.empty()) {
    log.write("nothing installable for this layout");
    return kExitNoUpdate;
  }
  if (!context.options.elevated) {
    waitForParent(context.options.parentPid);
    waitForViewers(viewerExecutables(plan));
  }

  if (needsElevation(context, plan)) {
    if (context.options.elevated) {
      return reportInstallFailure(context, log, InstallError::AccessDenied,
                                  "elevated but still not permitted", version);
    }
    const std::optional<int> code = runElevated(context, log);
    return code ? *code : handleElevationDeclined(context, log, version);
  }
  return installPlan(context, log, plan, version);
}

// ----------------------------------------------------------------- recovery

int runRecover(const Context &context, const Log &log) {
  if (!context.options.elevated) {
    waitForParent(context.options.parentPid);
    waitForViewers(recoveryViewers(context));
  }
  if (!hasPendingJournal(context.target.root)) {
    log.write("no interrupted install to recover");
    return kExitOk;
  }
  if (!canWriteDirectory(context.target.root)) {
    if (context.options.elevated) {
      log.write("recovery: elevated but the folder is still not writable");
      return kExitFailed;
    }
    const std::optional<int> code = runElevated(context, log);
    return code ? *code : handleElevationDeclined(context, log, std::nullopt);
  }

  std::string detail;
  const InstallError error = recoverInstall(context.target.root, &detail);
  if (error == InstallError::None) {
    log.write("recovery finished");
    return kExitOk;
  }
  log.write(std::string("recovery failed: ") + installErrorName(error) + " (" + detail + ")");
  showMessageAndOfferReleases(
      L"FarSightVNC could not finish repairing an interrupted update. It will try again the "
      L"next time FarSightVNC closes. Open the releases page to update manually?",
      MB_ICONWARNING);
  return kExitFailed;
}

// --------------------------------------------------------------------- main

int run(const std::vector<std::wstring> &args) {
  const std::optional<Options> parsed = parseArguments(args);
  if (!parsed) {
    return kExitUsage;
  }
  Context context;
  context.options = *parsed;
  if (!validate(context.options, context.target)) {
    return kExitRejected;
  }
  context.options.stateDir = fullPath(context.options.stateDir);
  context.pendingDir = joinPath(context.options.stateDir, kPendingDirName);
  context.lastResultPath = joinPath(context.options.stateDir, L"last-result.txt");

  // One updater per installation; the elevated child runs under its parent's lock.
  UniqueHandle mutex;
  if (!context.options.elevated) {
    mutex.reset(::CreateMutexW(
        nullptr, FALSE, (std::wstring(kMutexPrefix) + fileName(context.options.stateDir)).c_str()));
    if (mutex.valid() && ::GetLastError() == ERROR_ALREADY_EXISTS) {
      return kExitOk;
    }
  }

  ensureDirectory(context.options.stateDir);
  const Log log(joinPath(context.options.stateDir, L"updater.log"));
  log.write(std::string("start: ") + (context.options.mode == Mode::Install ? "install" : "recover") +
            (context.options.elevated ? " (elevated)" : ""));
  log.write("viewer", context.options.viewer);

  return context.options.mode == Mode::Install ? runInstall(context, log)
                                               : runRecover(context, log);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  // First thing: never load DLLs from the application or current directory.
  ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
  try {
    return run(commandLineArguments());
  } catch (...) {
    return kExitInternal;
  }
}
