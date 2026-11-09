// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Transactional file replacement used by FarSightUpdater.exe. Never touches
// files that are not listed in the signed manifest. Crash-safe through a
// journal in <root>\.fsv-update\ (see docs/updater/DESIGN.md, "Install").

#pragma once

#include "Manifest.h"

#include <functional>

namespace fsv::update {

enum class InstallLayout {
  Full,       // root\x64\tvnviewer.exe, root\Win32\..., root\LICENSE.txt ...
  Standalone, // a lone directory with tvnviewer.exe of one architecture
};

struct InstallTarget {
  std::wstring root;   // full path, no trailing '\'
  InstallLayout layout = InstallLayout::Standalone;
  std::string arch;    // "x64" / "Win32": the running build's architecture
};

// Full if the viewer's directory is named x64 or Win32 (case-insensitive) and
// its parent contains LICENSE.txt; then root = parent. Otherwise Standalone
// with root = viewer directory.
InstallTarget detectInstallTarget(const std::wstring &viewerExePath, std::string_view arch);

struct PlannedFile {
  std::wstring relPath;  // relative to root, '\'-separated
  std::wstring source;   // staged file (stagedFilesRoot\<manifest path>)
  std::wstring target;   // root\relPath
  std::uint64_t size = 0;
  Sha256Digest sha256{};
};

// Full: every manifest file maps to the same relative path.
// Standalone: only files under "<arch>/" are installed, prefix stripped;
// other files are skipped. Empty result = nothing installable (error).
std::vector<PlannedFile> planInstall(const Manifest &manifest,
                                     const std::wstring &stagedFilesRoot,
                                     const InstallTarget &target);

// Full paths of every tvnviewer.exe the plan targets (to wait for), including
// ones that do not exist yet are harmless.
std::vector<std::wstring> viewerExecutables(const std::vector<PlannedFile> &plan);

inline constexpr wchar_t kWorkDirName[] = L".fsv-update";

enum class InstallError {
  None,
  InUse,          // a target file is open/running; nothing was changed
  NoSpace,        // nothing was changed
  AccessDenied,   // nothing was changed
  CopyFailed,     // copy into work dir failed; nothing was changed
  VerifyFailed,   // copied/installed bytes did not match the manifest
  RolledBack,     // swap failed, previous installation restored
  RollbackFailed, // swap failed AND restore failed: journal kept for recover
  Io,
};

const char *installErrorName(InstallError error);

// Test-only seams; production passes nullptr.
struct InstallHooks {
  // Called before each step ("copy", "swap", "verify", "commit") with an
  // index; returning false simulates a failure at that point.
  std::function<bool(const char *step, std::size_t index)> allowStep;
  // Called at the same points; returning true simulates process death (the
  // function returns immediately leaving journal/work files as they are).
  std::function<bool(const char *step, std::size_t index)> crashAt;
  std::function<std::optional<std::uint64_t>(const std::wstring &)> freeSpace;
};

// 1. refuse if a pending journal exists (call recoverInstall first);
// 2. check free space (sum of sizes + 1 MiB) and that no target is in use;
// 3. copy sources into <root>\.fsv-update\new\ and verify SHA-256 there
//    (closes the window between verification and use);
// 4. journal, then per file: target -> .fsv-update\backup\, new -> target;
// 5. verify installed files; 6. mark committed, delete work dir.
// Any failure in 4-5 restores the backups in reverse order.
InstallError applyInstall(const InstallTarget &target, const std::vector<PlannedFile> &plan,
                          std::string *detail, const InstallHooks *hooks = nullptr);

bool hasPendingJournal(const std::wstring &root);

// Uncommitted journal -> roll back to the previous files; committed -> finish
// cleanup. No journal -> None.
InstallError recoverInstall(const std::wstring &root, std::string *detail);

// ---------------------------------------------------------- process checks

// PIDs (excluding excludePid) of running processes whose full image path
// equals one of exePaths (case-insensitive). Processes that cannot be opened
// are ignored; the in-use file probe in applyInstall is the backstop.
std::vector<std::uint32_t> findProcessesByImage(const std::vector<std::wstring> &exePaths,
                                                std::uint32_t excludePid = 0);

// Blocks until none are running. Polls every pollMs with negligible CPU.
// Returns false if `cancelled` returns true.
bool waitForProcessesToExit(const std::vector<std::wstring> &exePaths,
                            const std::function<bool()> &cancelled,
                            std::uint32_t pollMs = 2000);

// True if the file exists and cannot be opened exclusively for write
// (e.g. a running executable).
bool isFileInUse(const std::wstring &path);

} // namespace fsv::update
