// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Tests of the transactional installer, crash recovery and process scan.

#include "TestHarness.h"
#include "TestKeys.h"

#include "updater-lib/Crypto.h"
#include "updater-lib/FsUtil.h"
#include "updater-lib/Installer.h"

#include <windows.h>

#include <chrono>
#include <map>
#include <string>
#include <vector>

using namespace fsv::update;
using fsv::test::TempDir;

namespace {

// ------------------------------------------------------------------ helpers

std::wstring extendedPath(const std::wstring &path) {
  const std::wstring full = fullPath(path);
  return full.rfind(L"\\\\?\\", 0) == 0 ? full : L"\\\\?\\" + full;
}

bool makeDirs(const std::wstring &path) {
  if (directoryExists(extendedPath(path))) {
    return true;
  }
  const std::wstring parent = parentDirectory(path);
  if (parent.empty() || !makeDirs(parent)) {
    return false;
  }
  return ::CreateDirectoryW(extendedPath(path).c_str(), nullptr) != FALSE;
}

bool writeText(const std::wstring &path, const std::string &content) {
  if (!makeDirs(parentDirectory(path))) {
    return false;
  }
  HANDLE file = ::CreateFileW(extendedPath(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const BOOL ok = content.empty() ||
                  ::WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written,
                              nullptr);
  ::CloseHandle(file);
  return ok != FALSE;
}

std::optional<std::string> readText(const std::wstring &path) {
  return readFile(extendedPath(path), 64ull * 1024 * 1024);
}

// Relative path -> SHA-256 hex (files) or "<dir>" (directories).
using Snapshot = std::map<std::wstring, std::string>;

void snapshotInto(const std::wstring &dir, const std::wstring &prefix, Snapshot &out) {
  WIN32_FIND_DATAW data{};
  HANDLE find = ::FindFirstFileExW((extendedPath(dir) + L"\\*").c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, nullptr, 0);
  if (find == INVALID_HANDLE_VALUE) {
    return;
  }
  do {
    const std::wstring name = data.cFileName;
    if (name == L"." || name == L"..") {
      continue;
    }
    const std::wstring rel = prefix.empty() ? name : prefix + L"\\" + name;
    const std::wstring full = joinPath(dir, name);
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      out[rel] = "<dir>";
      snapshotInto(full, rel, out);
    } else {
      const std::optional<Sha256Digest> digest = sha256File(extendedPath(full));
      out[rel] = digest ? toHex(*digest) : "<unreadable>";
    }
  } while (::FindNextFileW(find, &data));
  ::FindClose(find);
}

Snapshot snapshotOf(const std::wstring &root) {
  Snapshot snapshot;
  snapshotInto(root, L"", snapshot);
  return snapshot;
}

Sha256Digest digestOf(const std::string &content) {
  return sha256(fsv::test::asBytes(content));
}

// Holds a file open with sharing mode 0 (nobody else may open or delete it).
class ExclusiveLock {
public:
  explicit ExclusiveLock(const std::wstring &path)
      : m_handle(::CreateFileW(extendedPath(path).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr)) {}
  ~ExclusiveLock() { release(); }
  ExclusiveLock(const ExclusiveLock &) = delete;
  ExclusiveLock &operator=(const ExclusiveLock &) = delete;
  bool held() const { return m_handle != INVALID_HANDLE_VALUE; }
  void release() {
    if (m_handle != INVALID_HANDLE_VALUE) {
      ::CloseHandle(m_handle);
      m_handle = INVALID_HANDLE_VALUE;
    }
  }

private:
  HANDLE m_handle;
};

// ------------------------------------------------------------------ fixture

struct Entry {
  std::wstring rel;
  std::string oldContent; // empty: the file does not exist before the install
  std::string newContent;
};

std::string bulk(char fill, std::size_t size) { return std::string(size, fill); }

std::vector<Entry> entries() {
  return {
      {L"LICENSE.txt", "old license", "new license text"},
      {L"x64\\tvnviewer.exe", bulk('o', 100000), bulk('n', 300000)},
      {L"x64\\FarSightUpdater.exe", "", "new updater x64"},
      {L"Win32\\tvnviewer.exe", "", bulk('w', 70000)},
      {L"Win32\\lib\\x.dll", "", "new dll"},
      {L"docs\\\u03a9mega-\u00e9.txt", "", "unicode file"},
  };
}

// A Full installation (root\x64\tvnviewer.exe + LICENSE.txt) with user files,
// plus the staged sources and the resulting plan.
struct Env {
  Env() {
    root = tmp.child(L"root-\u00fcn\u00ef");
    const std::wstring staged = tmp.child(L"staged");
    for (const Entry &entry : entries()) {
      if (!entry.oldContent.empty()) {
        writeText(joinPath(root, entry.rel), entry.oldContent);
      }
    }
    writeText(joinPath(root, L"my.vnc"), "user connection file");
    writeText(joinPath(root, L"x64\\notes.txt"), "user notes");
    target = detectInstallTarget(joinPath(root, L"x64\\tvnviewer.exe"), "x64");
    for (const Entry &entry : entries()) {
      PlannedFile file;
      file.relPath = entry.rel;
      file.source = joinPath(staged, entry.rel);
      file.target = joinPath(root, entry.rel);
      file.size = entry.newContent.size();
      file.sha256 = digestOf(entry.newContent);
      writeText(file.source, entry.newContent);
      plan.push_back(file);
    }
    before = snapshotOf(root);
  }

  std::wstring path(const std::wstring &rel) const { return joinPath(root, rel); }
  bool workDirGone() const { return !directoryExists(extendedPath(path(kWorkDirName))); }

  bool unchanged() const { return snapshotOf(root) == before; }

  // Every target holds its new content; unrelated user files are untouched.
  bool fullyInstalled() const {
    for (const Entry &entry : entries()) {
      const std::optional<std::string> content = readText(path(entry.rel));
      if (!content || *content != entry.newContent) {
        return false;
      }
    }
    return readText(path(L"my.vnc")) == std::optional<std::string>("user connection file") &&
           readText(path(L"x64\\notes.txt")) == std::optional<std::string>("user notes") &&
           workDirGone();
  }

  TempDir tmp;
  std::wstring root;
  InstallTarget target;
  std::vector<PlannedFile> plan;
  Snapshot before;
};

// --------------------------------------------------------- layout and plan

Manifest manifestWith(std::initializer_list<const char *> paths) {
  Manifest manifest;
  manifest.version = *Version::parse("0.3.0a");
  manifest.packageName = "FarSightVNC-v0.3.0a.zip";
  for (const char *path : paths) {
    ManifestFile file;
    file.path = path;
    file.size = std::string(path).size();
    file.sha256 = digestOf(path);
    manifest.files.push_back(file);
  }
  return manifest;
}

void testDetectInstallTarget() {
  {
    TempDir tmp;
    writeText(tmp.child(L"root\\LICENSE.txt"), "license");
    writeText(tmp.child(L"root\\x64\\tvnviewer.exe"), "exe");
    const InstallTarget target =
        detectInstallTarget(tmp.child(L"root\\x64\\tvnviewer.exe"), "x64");
    CHECK(target.layout == InstallLayout::Full);
    CHECK(pathsEqual(target.root, tmp.child(L"root")));
    CHECK(target.arch == "x64");
    CHECK(!target.root.empty() && target.root.back() != L'\\');
  }
  {
    TempDir tmp;
    writeText(tmp.child(L"root\\LICENSE.txt"), "license");
    writeText(tmp.child(L"root\\WIN32\\tvnviewer.exe"), "exe");
    const InstallTarget target =
        detectInstallTarget(tmp.child(L"root\\WIN32\\tvnviewer.exe"), "Win32");
    CHECK(target.layout == InstallLayout::Full);
    CHECK(pathsEqual(target.root, tmp.child(L"root")));
  }
  {
    TempDir tmp; // an arch-named folder without LICENSE.txt next to it
    writeText(tmp.child(L"root\\x64\\tvnviewer.exe"), "exe");
    const InstallTarget target = detectInstallTarget(tmp.child(L"root\\x64\\tvnviewer.exe"), "x64");
    CHECK(target.layout == InstallLayout::Standalone);
    CHECK(pathsEqual(target.root, tmp.child(L"root\\x64")));
  }
  {
    TempDir tmp; // LICENSE.txt in the parent, but the folder is not an arch name
    writeText(tmp.child(L"LICENSE.txt"), "license");
    writeText(tmp.child(L"viewer\\tvnviewer.exe"), "exe");
    const InstallTarget target = detectInstallTarget(tmp.child(L"viewer\\tvnviewer.exe"), "x64");
    CHECK(target.layout == InstallLayout::Standalone);
    CHECK(pathsEqual(target.root, tmp.child(L"viewer")));
  }
}

void testPlanInstall() {
  const Manifest manifest = manifestWith({"x64/tvnviewer.exe", "x64/FarSightUpdater.exe",
                                          "Win32/tvnviewer.exe", "LICENSE.txt"});
  const std::wstring staged = L"C:\\state\\pending\\files";
  {
    InstallTarget full;
    full.root = L"C:\\app";
    full.layout = InstallLayout::Full;
    full.arch = "x64";
    const std::vector<PlannedFile> plan = planInstall(manifest, staged, full);
    CHECK(plan.size() == 4);
    if (plan.size() == 4) {
      CHECK(plan[0].relPath == L"x64\\tvnviewer.exe");
      CHECK(plan[0].source == L"C:\\state\\pending\\files\\x64\\tvnviewer.exe");
      CHECK(plan[0].target == L"C:\\app\\x64\\tvnviewer.exe");
      CHECK(plan[0].size == manifest.files[0].size);
      CHECK(plan[0].sha256 == manifest.files[0].sha256);
      CHECK(plan[3].relPath == L"LICENSE.txt");
      CHECK(plan[3].target == L"C:\\app\\LICENSE.txt");
    }
    const std::vector<std::wstring> viewers = viewerExecutables(plan);
    CHECK(viewers.size() == 2);
    CHECK(viewers == (std::vector<std::wstring>{L"C:\\app\\x64\\tvnviewer.exe",
                                                L"C:\\app\\Win32\\tvnviewer.exe"}));
  }
  {
    InstallTarget standalone;
    standalone.root = L"C:\\tools\\vnc";
    standalone.layout = InstallLayout::Standalone;
    standalone.arch = "x64";
    const std::vector<PlannedFile> plan = planInstall(manifest, staged, standalone);
    CHECK(plan.size() == 2);
    if (plan.size() == 2) {
      CHECK(plan[0].relPath == L"tvnviewer.exe");
      CHECK(plan[0].source == L"C:\\state\\pending\\files\\x64\\tvnviewer.exe");
      CHECK(plan[0].target == L"C:\\tools\\vnc\\tvnviewer.exe");
      CHECK(plan[1].relPath == L"FarSightUpdater.exe");
    }
    CHECK(viewerExecutables(plan) == std::vector<std::wstring>{L"C:\\tools\\vnc\\tvnviewer.exe"});

    standalone.arch = "arm64";
    CHECK(planInstall(manifest, staged, standalone).empty());
  }
  CHECK(viewerExecutables({}).empty());
  CHECK(std::string(installErrorName(InstallError::None)) == "None");
  CHECK(std::string(installErrorName(InstallError::RollbackFailed)) == "RollbackFailed");
  CHECK(std::string(installErrorName(InstallError::AccessDenied)) == "AccessDenied");
}

// -------------------------------------------------------------- install ok

void testApplySuccess() {
  Env env;
  std::string detail;
  CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::None);
  CHECK(env.fullyInstalled());
  CHECK(!hasPendingJournal(env.root));
  CHECK(directoryExists(extendedPath(env.path(L"Win32\\lib"))));
  const std::optional<Sha256Digest> digest = sha256File(env.path(L"x64\\tvnviewer.exe"));
  CHECK(digest.has_value() && *digest == env.plan[1].sha256);

  // Idempotent re-run over the new files (everything already current).
  CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::None);
  CHECK(env.fullyInstalled());
}

void testLongPath() {
  TempDir tmp;
  std::wstring root = tmp.child(L"root");
  for (int i = 0; i < 6; ++i) {
    root = joinPath(root, std::wstring(50, static_cast<wchar_t>(L'a' + i)));
  }
  CHECK(root.size() > 300);
  writeText(joinPath(root, L"x64\\tvnviewer.exe"), "old long");
  writeText(joinPath(root, L"keep.txt"), "keep");
  const std::string content = "new long path content";
  const std::wstring source = tmp.child(L"src\\x64\\tvnviewer.exe");
  writeText(source, content);

  InstallTarget target;
  target.root = root;
  target.layout = InstallLayout::Full;
  target.arch = "x64";
  PlannedFile file;
  file.relPath = L"x64\\tvnviewer.exe";
  file.source = source;
  file.target = joinPath(root, file.relPath);
  file.size = content.size();
  file.sha256 = digestOf(content);
  std::string detail;
  CHECK(applyInstall(target, {file}, &detail, nullptr) == InstallError::None);
  CHECK(readText(file.target) == std::optional<std::string>(content));
  CHECK(readText(joinPath(root, L"keep.txt")) == std::optional<std::string>("keep"));
  CHECK(!directoryExists(extendedPath(joinPath(root, kWorkDirName))));
}

// ----------------------------------------------------------- preconditions

void testPreconditions() {
  {
    Env env;
    InstallHooks hooks;
    hooks.freeSpace = [](const std::wstring &) { return std::optional<std::uint64_t>(0); };
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, &hooks) == InstallError::NoSpace);
    CHECK(env.unchanged());
    // Exactly enough space passes (sum of sizes + 1 MiB).
    std::uint64_t sum = 1024 * 1024;
    for (const PlannedFile &file : env.plan) {
      sum += file.size;
    }
    hooks.freeSpace = [sum](const std::wstring &) { return std::optional<std::uint64_t>(sum - 1); };
    CHECK(applyInstall(env.target, env.plan, &detail, &hooks) == InstallError::NoSpace);
    hooks.freeSpace = [sum](const std::wstring &) { return std::optional<std::uint64_t>(sum); };
    CHECK(applyInstall(env.target, env.plan, &detail, &hooks) == InstallError::None);
    CHECK(env.fullyInstalled());
  }
  {
    Env env;
    ExclusiveLock lock(env.path(L"x64\\tvnviewer.exe"));
    CHECK(lock.held());
    CHECK(isFileInUse(env.path(L"x64\\tvnviewer.exe")));
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::InUse);
    CHECK(detail.find("tvnviewer.exe") != std::string::npos);
    lock.release();
    CHECK(env.unchanged());
    CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::None);
  }
  {
    Env env; // a read-only target cannot be replaced by this token
    const std::wstring file = env.path(L"x64\\tvnviewer.exe");
    ::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::AccessDenied);
    ::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
    CHECK(env.unchanged());
  }
  {
    Env env; // a pending journal blocks a second transaction
    writeText(env.path(L".fsv-update\\journal"), "begin 0\n");
    const Snapshot withJournal = snapshotOf(env.root);
    CHECK(hasPendingJournal(env.root));
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::Io);
    CHECK(detail.find("pending journal") != std::string::npos);
    CHECK(snapshotOf(env.root) == withJournal);
    CHECK(recoverInstall(env.root, &detail) == InstallError::None);
    CHECK(!hasPendingJournal(env.root));
    CHECK(env.unchanged());
  }
  {
    Env env; // source differs from the manifest: caught at copy time
    env.plan[2].sha256 = digestOf("something else");
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::VerifyFailed);
    CHECK(env.unchanged());
  }
  {
    Env env; // missing source -> CopyFailed
    ::DeleteFileW(extendedPath(env.plan[3].source).c_str());
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::CopyFailed);
    CHECK(env.unchanged());
  }
  {
    Env env; // a plan entry that would leave the root is refused
    env.plan[0].relPath = L"..\\evil.txt";
    env.plan[0].target = joinPath(env.root, L"..\\evil.txt");
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::Io);
    CHECK(env.unchanged());
    CHECK(applyInstall(env.target, {}, &detail, nullptr) == InstallError::Io);
  }
}

// -------------------------------------------- failure injection / rollback

void testStepFailures() {
  const std::size_t count = entries().size();
  struct Case {
    const char *step;
    std::size_t index;
    InstallError expected;
  };
  std::vector<Case> cases;
  for (std::size_t i = 0; i < count; ++i) {
    cases.push_back({"copy", i, InstallError::CopyFailed});
    cases.push_back({"swap", i, InstallError::RolledBack});
    cases.push_back({"swap-mid", i, InstallError::RolledBack});
    cases.push_back({"verify", i, InstallError::RolledBack});
  }
  cases.push_back({"commit", 0, InstallError::RolledBack});

  for (const Case &item : cases) {
    Env env;
    InstallHooks hooks;
    hooks.allowStep = [&](const char *step, std::size_t index) {
      return !(std::string(step) == item.step && index == item.index);
    };
    std::string detail;
    const InstallError result = applyInstall(env.target, env.plan, &detail, &hooks);
    if (result != item.expected) {
      std::printf("  step %s/%zu returned %s (%s)\n", item.step, item.index,
                  installErrorName(result), detail.c_str());
    }
    CHECK(result == item.expected);
    CHECK(env.unchanged());
    CHECK(env.workDirGone());
    CHECK(!hasPendingJournal(env.root));
  }
}

// ------------------------------------------------------------ crash recovery

void testCrashRecovery() {
  const std::size_t count = entries().size();
  struct Point {
    const char *step;
    std::size_t index;
  };
  std::vector<Point> points;
  for (std::size_t i = 0; i < count; ++i) {
    points.push_back({"copy", i});
    points.push_back({"swap", i});
    points.push_back({"swap-mid", i});
    points.push_back({"verify", i});
  }
  points.push_back({"commit", 0});
  points.push_back({"after-commit", 0});

  for (const Point &point : points) {
    Env env;
    InstallHooks hooks;
    hooks.crashAt = [&](const char *step, std::size_t index) {
      return std::string(step) == point.step && index == point.index;
    };
    std::string detail;
    CHECK(applyInstall(env.target, env.plan, &detail, &hooks) == InstallError::Io);

    const bool committed = std::string(point.step) == "after-commit";
    CHECK(!committed || hasPendingJournal(env.root));
    const InstallError recovered = recoverInstall(env.root, &detail);
    if (recovered != InstallError::None) {
      std::printf("  crash %s/%zu: recover returned %s (%s)\n", point.step, point.index,
                  installErrorName(recovered), detail.c_str());
    }
    CHECK(recovered == InstallError::None);
    CHECK(!hasPendingJournal(env.root));
    CHECK(env.workDirGone());
    CHECK(committed ? env.fullyInstalled() : env.unchanged());

    CHECK(recoverInstall(env.root, &detail) == InstallError::None); // idempotent
  }
}

void testRecoverWithTornJournal() {
  std::string detail;
  InstallHooks hooks;

  // Old file moved to backup, new one not yet in place (partial swap), and the
  // next line was being written when the process died.
  Env env;
  hooks.crashAt = [](const char *step, std::size_t i) {
    return std::string(step) == "swap-mid" && i == 1;
  };
  CHECK(applyInstall(env.target, env.plan, &detail, &hooks) == InstallError::Io);
  const std::wstring journal = env.path(L".fsv-update\\journal");
  const std::optional<std::string> text = readText(journal);
  CHECK(text.has_value());
  CHECK(writeText(journal, text.value_or("") + "swap 2"));
  CHECK(recoverInstall(env.root, &detail) == InstallError::None);
  CHECK(env.unchanged());
  CHECK(env.workDirGone());

  // The last line itself is cut short.
  Env env2;
  hooks.crashAt = [](const char *step, std::size_t i) {
    return std::string(step) == "swap" && i == 3;
  };
  CHECK(applyInstall(env2.target, env2.plan, &detail, &hooks) == InstallError::Io);
  const std::wstring journal2 = env2.path(L".fsv-update\\journal");
  std::string cut = readText(journal2).value_or("");
  CHECK(cut.size() > 4 && cut.back() == '\n');
  cut.resize(cut.size() - 3); // "swap 3 0\n" -> "swap 3"
  CHECK(writeText(journal2, cut));
  CHECK(recoverInstall(env2.root, &detail) == InstallError::None);
  CHECK(env2.unchanged());

  // Crash before the journal exists: only a stale work dir is left.
  Env env3;
  CHECK(writeText(env3.path(L".fsv-update\\new\\junk.bin"), "junk"));
  CHECK(!hasPendingJournal(env3.root));
  CHECK(recoverInstall(env3.root, &detail) == InstallError::None);
  CHECK(env3.workDirGone());
  CHECK(env3.unchanged());
  CHECK(recoverInstall(env3.root, &detail) == InstallError::None);
}

void testRollbackFailed() {
  Env env;
  InstallHooks hooks;
  std::optional<ExclusiveLock> lock;
  hooks.allowStep = [&](const char *step, std::size_t index) {
    if (std::string(step) == "swap" && index == 3) {
      // Swaps 0..2 are installed: pin a just-installed file so its rollback fails.
      lock.emplace(env.path(L"x64\\tvnviewer.exe"));
      return false;
    }
    return true;
  };
  std::string detail;
  CHECK(applyInstall(env.target, env.plan, &detail, &hooks) == InstallError::RollbackFailed);
  CHECK(lock.has_value() && lock->held());
  CHECK(detail.find("tvnviewer.exe") != std::string::npos);
  CHECK(hasPendingJournal(env.root));
  // A second install is refused while the journal exists.
  CHECK(applyInstall(env.target, env.plan, &detail, nullptr) == InstallError::Io);

  // Still locked: recovery fails too and names the file.
  CHECK(recoverInstall(env.root, &detail) == InstallError::RollbackFailed);
  CHECK(detail.find("tvnviewer.exe") != std::string::npos);
  CHECK(hasPendingJournal(env.root));

  if (lock) {
    lock->release();
  }
  CHECK(recoverInstall(env.root, &detail) == InstallError::None);
  CHECK(!hasPendingJournal(env.root));
  CHECK(env.workDirGone());
  CHECK(env.unchanged());
}

// ------------------------------------------------------------ process scan

struct ChildProcess {
  ChildProcess() = default;
  ChildProcess(const ChildProcess &) = delete;
  ChildProcess &operator=(const ChildProcess &) = delete;
  ~ChildProcess() {
    if (handle != nullptr) {
      ::TerminateProcess(handle, 1);
      ::WaitForSingleObject(handle, 5000);
      ::CloseHandle(handle);
    }
  }
  bool start(const std::wstring &exe, int sleepMs) {
    std::wstring commandLine = L"\"" + exe + L"\" --sleep-ms " + std::to_wstring(sleepMs);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!::CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
      return false;
    }
    ::CloseHandle(info.hThread);
    handle = info.hProcess;
    pid = info.dwProcessId;
    return true;
  }
  bool exited(DWORD timeoutMs) const {
    return handle != nullptr && ::WaitForSingleObject(handle, timeoutMs) == WAIT_OBJECT_0;
  }
  HANDLE handle = nullptr;
  DWORD pid = 0;
};

void testProcessScan() {
  TempDir tmp;
  const std::wstring self = currentModulePath();
  const std::wstring exeA = tmp.child(L"a\\tvnviewer.exe");
  const std::wstring exeB = tmp.child(L"b\\tvnviewer.exe");
  CHECK(makeDirs(tmp.child(L"a")) && makeDirs(tmp.child(L"b")));
  CHECK(::CopyFileW(self.c_str(), exeA.c_str(), FALSE) != FALSE);
  CHECK(::CopyFileW(self.c_str(), exeB.c_str(), FALSE) != FALSE);

  CHECK(findProcessesByImage({exeA}).empty());
  CHECK(!isFileInUse(exeA));
  CHECK(!isFileInUse(tmp.child(L"missing.exe")));
  CHECK(waitForProcessesToExit({exeA}, [] { return true; }, 100)); // nothing running

  ChildProcess child;
  if (!child.start(exeA, 3000)) {
    CHECK(!"could not start the helper process");
    return;
  }
  const auto started = std::chrono::steady_clock::now();
  const std::function<bool()> cancelAfter15s = [started] {
    return std::chrono::steady_clock::now() - started > std::chrono::seconds(15);
  };

  const std::vector<std::uint32_t> found = findProcessesByImage({exeA});
  CHECK(found == std::vector<std::uint32_t>{child.pid});
  CHECK(findProcessesByImage({exeB}).empty()); // same name, other directory
  CHECK(findProcessesByImage({exeA}, child.pid).empty());
  CHECK(findProcessesByImage({exeB, exeA}) == found);
  CHECK(isFileInUse(exeA));
  CHECK(!isFileInUse(exeB));
  CHECK(!canReplaceFile(exeA));

  // Immediate cancel while it runs.
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(!waitForProcessesToExit({exeA}, [] { return true; }, 200));
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));

  // Waiting returns true once it has exited (cancel guard trips after 15 s).
  CHECK(waitForProcessesToExit({exeA}, cancelAfter15s, 200));
  CHECK(child.exited(0));
  bool released = false;
  for (int i = 0; i < 50 && !released; ++i) {
    released = !isFileInUse(exeA);
    if (!released) {
      ::Sleep(100);
    }
  }
  CHECK(released);
  CHECK(findProcessesByImage({exeA}).empty());
  CHECK(canReplaceFile(exeA));
}

} // namespace

void runInstallerTests() {
  fsv::test::beginSuite("Installer");
  testDetectInstallTarget();
  testPlanInstall();
  testApplySuccess();
  testLongPath();
  testPreconditions();
  testStepFailures();
  testCrashRecovery();
  testRecoverWithTornJournal();
  testRollbackFailed();
  testProcessScan();
}
