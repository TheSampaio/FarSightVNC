// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Transactional installer (see docs/updater/DESIGN.md, "Install transaction").
//
// Work directory <root>\.fsv-update\ holds new\ (verified copies), backup\ (the
// replaced files) and journal. Journal lines (UTF-8, LF, flushed one by one):
//   file <i> <relPath>      the plan, written first so recovery needs no manifest
//   begin <n>               swapping starts
//   swap <i> <0|1>          before swap i (1 = an old file existed and is moved)
//   done <i>                after swap i
//   committed               all swaps verified
// Every path handed to the Win32 API goes through extended() so that long and
// Unicode paths work regardless of the process's long-path awareness.

#include "Installer.h"

#include "Crypto.h"
#include "FsUtil.h"
#include "Win32Util.h"

#include <algorithm>
#include <charconv>
#include <string>

namespace fsv::update {

namespace {

constexpr std::uint64_t kSpaceMarginBytes = 1024 * 1024;
constexpr std::uint64_t kMaxJournalBytes = 1024 * 1024;
constexpr std::size_t kMaxJournalEntries = 4096;
constexpr wchar_t kNewDirName[] = L"new";
constexpr wchar_t kBackupDirName[] = L"backup";
constexpr wchar_t kJournalName[] = L"journal";

bool equalsNoCase(std::wstring_view a, std::wstring_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  if (a.empty()) {
    return true;
  }
  return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// A top-level ".exe" manifest entry (no sub-folder).
bool isTopLevelExecutable(std::string_view path) {
  constexpr std::string_view extension = ".exe";
  if (path.size() <= extension.size() || path.find('/') != std::string_view::npos) {
    return false;
  }
  const std::string_view tail = path.substr(path.size() - extension.size());
  for (std::size_t i = 0; i < extension.size(); ++i) {
    const char c = tail[i] >= 'A' && tail[i] <= 'Z' ? static_cast<char>(tail[i] + 32) : tail[i];
    if (c != extension[i]) {
      return false;
    }
  }
  return true;
}

// "\\?\"-prefixed absolute form of a path (no MAX_PATH limit).
std::wstring extended(const std::wstring &path) {
  const std::wstring full = fullPath(path);
  if (full.rfind(L"\\\\?\\", 0) == 0) {
    return full;
  }
  if (full.rfind(L"\\\\", 0) == 0) {
    return L"\\\\?\\UNC\\" + full.substr(2);
  }
  return L"\\\\?\\" + full;
}

std::string errorText(DWORD error) { return "error " + std::to_string(error); }

// A relative path that can never leave the install root.
bool isSafeRelative(const std::wstring &rel) {
  if (rel.empty()) {
    return false;
  }
  std::size_t begin = 0;
  while (begin <= rel.size()) {
    std::size_t end = rel.find(L'\\', begin);
    if (end == std::wstring::npos) {
      end = rel.size();
    }
    const std::wstring_view segment = std::wstring_view(rel).substr(begin, end - begin);
    if (segment.empty() || segment == L"." || segment == L"..") {
      return false;
    }
    for (const wchar_t c : segment) {
      if (c < 0x20 || c == L':' || c == L'/' || c == L'<' || c == L'>' || c == L'"' ||
          c == L'|' || c == L'?' || c == L'*') {
        return false;
      }
    }
    begin = end + 1;
  }
  return true;
}

struct WorkPaths {
  explicit WorkPaths(const std::wstring &rootDir)
      : root(rootDir),
        work(joinPath(rootDir, kWorkDirName)),
        newDir(joinPath(work, kNewDirName)),
        backupDir(joinPath(work, kBackupDirName)),
        journal(joinPath(work, kJournalName)) {}

  std::wstring target(const std::wstring &rel) const { return joinPath(root, rel); }
  std::wstring staged(const std::wstring &rel) const { return joinPath(newDir, rel); }
  std::wstring backup(const std::wstring &rel) const { return joinPath(backupDir, rel); }

  std::wstring root;
  std::wstring work;
  std::wstring newDir;
  std::wstring backupDir;
  std::wstring journal;
};

bool ensureDir(const std::wstring &path) {
  if (directoryExists(path)) {
    return true;
  }
  const std::wstring parent = parentDirectory(path);
  if (parent.empty() || parent == path || !ensureDir(parent)) {
    return false;
  }
  return ::CreateDirectoryW(path.c_str(), nullptr) != FALSE ||
         (::GetLastError() == ERROR_ALREADY_EXISTS && directoryExists(path));
}

bool ensureParentDir(const std::wstring &extendedFile) {
  return ensureDir(parentDirectory(extendedFile));
}

bool deleteFileForce(const std::wstring &extendedFile) {
  const DWORD attributes = ::GetFileAttributesW(extendedFile.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
    ::SetFileAttributesW(extendedFile.c_str(),
                         attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
  }
  return ::DeleteFileW(extendedFile.c_str()) != FALSE;
}

bool moveFile(const std::wstring &extendedFrom, const std::wstring &extendedTo, DWORD flags) {
  return ::MoveFileExW(extendedFrom.c_str(), extendedTo.c_str(), flags | MOVEFILE_WRITE_THROUGH) !=
         FALSE;
}

// Removes now-empty directories between a removed file and the install root.
void removeEmptyParents(const std::wstring &extendedFile, const std::wstring &extendedRoot) {
  std::wstring dir = parentDirectory(extendedFile);
  while (dir.size() > extendedRoot.size()) {
    if (!::RemoveDirectoryW(dir.c_str())) {
      return;
    }
    dir = parentDirectory(dir);
  }
}

bool matchesPlan(const std::wstring &path, const PlannedFile &file) {
  std::uint64_t size = 0;
  const std::optional<Sha256Digest> digest = sha256File(extended(path), &size);
  return digest.has_value() && size == file.size && *digest == file.sha256;
}

// ----------------------------------------------------------------- journal

class JournalWriter {
public:
  bool open(const std::wstring &path) {
    m_file.reset(::CreateFileW(extended(path).c_str(), GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    return m_file.valid();
  }

  // Appends one line and flushes it to disk.
  bool append(const std::string &line) {
    if (!m_file.valid()) {
      return false;
    }
    const std::string text = line + '\n';
    DWORD written = 0;
    return ::WriteFile(m_file.get(), text.data(), static_cast<DWORD>(text.size()), &written,
                       nullptr) != FALSE &&
           written == text.size() && ::FlushFileBuffers(m_file.get()) != FALSE;
  }

  void close() { m_file.reset(); }

private:
  UniqueHandle m_file;
};

struct JournalState {
  std::vector<std::wstring> rels; // by index
  std::vector<int> swapHadOld;    // -1: no swap line; 0 / 1: the swap line's flag
  bool committed = false;
  bool corrupt = false;
};

std::optional<std::size_t> parseIndex(std::string_view text) {
  std::size_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size() ||
      value >= kMaxJournalEntries) {
    return std::nullopt;
  }
  return value;
}

void applyFileLine(JournalState &state, std::string_view rest) {
  const std::size_t space = rest.find(' ');
  const std::optional<std::size_t> index =
      parseIndex(rest.substr(0, space == std::string_view::npos ? rest.size() : space));
  const std::wstring rel =
      space == std::string_view::npos ? std::wstring{} : utf8ToWide(rest.substr(space + 1));
  if (!index || !isSafeRelative(rel)) {
    state.corrupt = true;
    return;
  }
  if (state.rels.size() <= *index) {
    state.rels.resize(*index + 1);
    state.swapHadOld.resize(*index + 1, -1);
  }
  state.rels[*index] = rel;
}

void applySwapLine(JournalState &state, std::string_view rest) {
  const std::size_t space = rest.find(' ');
  const std::optional<std::size_t> index =
      parseIndex(rest.substr(0, space == std::string_view::npos ? rest.size() : space));
  const std::string_view flag =
      space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
  if (!index || *index >= state.rels.size() || state.rels[*index].empty() ||
      (flag != "0" && flag != "1")) {
    state.corrupt = true;
    return;
  }
  state.swapHadOld[*index] = flag == "1" ? 1 : 0;
}

// An unterminated last line (torn write) is ignored: lines are flushed
// completely before the step they announce, so it announces nothing that ran.
JournalState parseJournal(std::string_view text) {
  JournalState state;
  const std::size_t complete = text.rfind('\n');
  text = complete == std::string_view::npos ? std::string_view{} : text.substr(0, complete + 1);
  while (!text.empty()) {
    const std::size_t newline = text.find('\n');
    std::string_view line = text.substr(0, newline);
    text.remove_prefix(newline + 1);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.rfind("file ", 0) == 0) {
      applyFileLine(state, line.substr(5));
    } else if (line.rfind("swap ", 0) == 0) {
      applySwapLine(state, line.substr(5));
    } else if (line == "committed") {
      state.committed = true;
    }
  }
  return state;
}

// ---------------------------------------------------------------- rollback

// Restores one file. Handles every partial state of a swap: nothing moved,
// backup moved but new not placed, both moved. Idempotent. Returns "" on success.
std::string rollbackOne(const WorkPaths &paths, const std::wstring &rel, int hadOld) {
  const std::wstring target = extended(paths.target(rel));
  const std::wstring backup = extended(paths.backup(rel));
  const std::string name = wideToUtf8(rel);

  if (hadOld == 0) {
    if (fileExists(target) && !deleteFileForce(target)) {
      return "cannot remove new file " + name + " (" + errorText(::GetLastError()) + ")";
    }
    removeEmptyParents(target, extended(paths.root));
    return {};
  }

  if (fileExists(backup)) {
    if (fileExists(target) && !deleteFileForce(target)) {
      return "cannot remove new file " + name + " (" + errorText(::GetLastError()) + ")";
    }
    if (!ensureParentDir(target) ||
        !moveFile(backup, target, MOVEFILE_REPLACE_EXISTING)) {
      return "cannot restore backup of " + name + " (" + errorText(::GetLastError()) + ")";
    }
    return {};
  }
  if (!fileExists(target)) {
    return "previous file lost, backup missing: " + name;
  }
  return {}; // the old file never left its place
}

// Rolls back every swapped index in reverse order; keeps going after a failure
// and reports the first one in `failure`.
bool rollbackAll(const WorkPaths &paths, const JournalState &state, std::string &failure) {
  bool ok = true;
  for (std::size_t i = state.rels.size(); i-- > 0;) {
    if (i >= state.swapHadOld.size() || state.swapHadOld[i] < 0) {
      continue;
    }
    const std::string problem = rollbackOne(paths, state.rels[i], state.swapHadOld[i]);
    if (!problem.empty()) {
      if (ok) {
        failure = problem;
      }
      ok = false;
    }
  }
  return ok;
}

// ---------------------------------------------------------------- the run

enum class Gate { Proceed, Fail, Crash };

class InstallRun {
public:
  InstallRun(const InstallTarget &target, const std::vector<PlannedFile> &plan,
             const InstallHooks *hooks)
      : m_paths(target.root), m_plan(plan), m_hooks(hooks) {
    m_state.rels.reserve(plan.size());
    for (const PlannedFile &file : plan) {
      m_state.rels.push_back(file.relPath);
    }
    m_state.swapHadOld.assign(plan.size(), -1);
  }

  InstallError run() {
    if (const InstallError error = checkPlan(); error != InstallError::None) {
      return error;
    }
    if (const InstallError error = checkPreconditions(); error != InstallError::None) {
      return error;
    }
    if (const InstallError error = stage(); error != InstallError::None) {
      return m_crashed ? InstallError::Io : abandon(error);
    }
    if (const InstallError error = swapAndVerify(); error != InstallError::None) {
      return m_crashed ? InstallError::Io : rollBack(error);
    }
    return commit();
  }

  const std::string &detail() const { return m_detail; }

private:
  InstallError fail(InstallError error, std::string detail) {
    m_detail = std::move(detail);
    return error;
  }

  Gate gate(const char *step, std::size_t index) {
    if (m_hooks != nullptr && m_hooks->crashAt && m_hooks->crashAt(step, index)) {
      m_crashed = true;
      return Gate::Crash;
    }
    if (m_hooks != nullptr && m_hooks->allowStep && !m_hooks->allowStep(step, index)) {
      return Gate::Fail;
    }
    return Gate::Proceed;
  }

  std::string relName(std::size_t i) const { return wideToUtf8(m_plan[i].relPath); }

  InstallError checkPlan() {
    if (m_plan.empty()) {
      return fail(InstallError::Io, "empty install plan");
    }
    if (!directoryExists(extended(m_paths.root))) {
      return fail(InstallError::Io, "install root does not exist");
    }
    for (const PlannedFile &file : m_plan) {
      if (!isSafeRelative(file.relPath) || !pathsEqual(file.target, m_paths.target(file.relPath))) {
        return fail(InstallError::Io, "unsafe or inconsistent plan entry");
      }
    }
    return InstallError::None;
  }

  InstallError checkPreconditions() {
    if (hasPendingJournal(m_paths.root)) {
      return fail(InstallError::Io, "pending journal; run recover");
    }
    if (!enoughSpace()) {
      return fail(InstallError::NoSpace, "not enough free disk space");
    }
    for (std::size_t i = 0; i < m_plan.size(); ++i) {
      if (isFileInUse(extended(m_paths.target(m_plan[i].relPath)))) {
        return fail(InstallError::InUse, "in use: " + relName(i));
      }
    }
    return checkAccess();
  }

  bool enoughSpace() const {
    std::uint64_t needed = kSpaceMarginBytes;
    for (const PlannedFile &file : m_plan) {
      needed += file.size;
    }
    const std::optional<std::uint64_t> available =
        (m_hooks != nullptr && m_hooks->freeSpace) ? m_hooks->freeSpace(m_paths.root)
                                                   : freeSpaceBytes(m_paths.root);
    return !available.has_value() || *available >= needed;
  }

  InstallError checkAccess() {
    if (!canWriteDirectory(extended(m_paths.root))) {
      return fail(InstallError::AccessDenied, "install folder is not writable");
    }
    for (std::size_t i = 0; i < m_plan.size(); ++i) {
      const std::wstring target = extended(m_paths.target(m_plan[i].relPath));
      const bool parentExists = directoryExists(parentDirectory(target));
      if (!fileExists(target) && !parentExists) {
        continue; // created later inside the writable root
      }
      if (!canReplaceFile(target)) {
        return fail(InstallError::AccessDenied, "cannot replace: " + relName(i));
      }
    }
    return InstallError::None;
  }

  // Creates the work dir and journal, copies every source and verifies the copy.
  InstallError stage() {
    const std::wstring work = extended(m_paths.work);
    if (directoryExists(work) && !removeTree(m_paths.work, m_paths.root)) {
      return fail(InstallError::Io, "cannot remove a stale work directory");
    }
    if (!ensureDir(extended(m_paths.newDir)) || !ensureDir(extended(m_paths.backupDir)) ||
        !m_journal.open(m_paths.journal)) {
      return fail(InstallError::Io, "cannot create the work directory");
    }
    for (std::size_t i = 0; i < m_plan.size(); ++i) {
      const std::string name = relName(i);
      if (name.empty() || !m_journal.append("file " + std::to_string(i) + " " + name)) {
        return fail(InstallError::Io, "cannot write the journal");
      }
    }
    for (std::size_t i = 0; i < m_plan.size(); ++i) {
      if (const InstallError error = copyOne(i); error != InstallError::None) {
        return error;
      }
    }
    return m_journal.append("begin " + std::to_string(m_plan.size()))
               ? InstallError::None
               : fail(InstallError::Io, "cannot write the journal");
  }

  InstallError copyOne(std::size_t i) {
    const Gate step = gate("copy", i);
    if (step == Gate::Crash) {
      return InstallError::Io;
    }
    if (step == Gate::Fail) {
      return fail(InstallError::CopyFailed, "copy refused: " + relName(i));
    }
    const std::wstring destination = extended(m_paths.staged(m_plan[i].relPath));
    if (!ensureParentDir(destination) ||
        !::CopyFileExW(extended(m_plan[i].source).c_str(), destination.c_str(), nullptr, nullptr,
                       nullptr, COPY_FILE_FAIL_IF_EXISTS)) {
      return fail(InstallError::CopyFailed,
                  "cannot copy " + relName(i) + " (" + errorText(::GetLastError()) + ")");
    }
    if (!matchesPlan(m_paths.staged(m_plan[i].relPath), m_plan[i])) {
      return fail(InstallError::VerifyFailed, "copy does not match the manifest: " + relName(i));
    }
    return InstallError::None;
  }

  InstallError swapAndVerify() {
    for (std::size_t i = 0; i < m_plan.size(); ++i) {
      if (const InstallError error = swapOne(i); error != InstallError::None) {
        return error;
      }
    }
    for (std::size_t i = 0; i < m_plan.size(); ++i) {
      const Gate step = gate("verify", i);
      if (step == Gate::Crash) {
        return InstallError::Io;
      }
      if (step == Gate::Fail || !matchesPlan(m_paths.target(m_plan[i].relPath), m_plan[i])) {
        return fail(InstallError::VerifyFailed, "installed file failed verification: " + relName(i));
      }
    }
    return InstallError::None;
  }

  InstallError swapOne(std::size_t i) {
    const std::wstring &rel = m_plan[i].relPath;
    const std::wstring target = extended(m_paths.target(rel));
    const std::wstring backup = extended(m_paths.backup(rel));
    const std::wstring staged = extended(m_paths.staged(rel));
    const int hadOld = fileExists(target) ? 1 : 0;

    m_state.swapHadOld[i] = hadOld; // before the line: rollback must consider it
    if (!m_journal.append("swap " + std::to_string(i) + " " + std::to_string(hadOld))) {
      return fail(InstallError::Io, "cannot write the journal");
    }
    if (const Gate step = gate("swap", i); step != Gate::Proceed) {
      return step == Gate::Crash ? InstallError::Io
                                 : fail(InstallError::Io, "swap refused: " + relName(i));
    }
    if (!ensureParentDir(target)) {
      return fail(InstallError::Io, "cannot create the folder of " + relName(i));
    }
    if (hadOld == 1 && (!ensureParentDir(backup) || !moveFile(target, backup, 0))) {
      return fail(InstallError::Io,
                  "cannot move " + relName(i) + " to backup (" + errorText(::GetLastError()) + ")");
    }
    if (const Gate step = gate("swap-mid", i); step != Gate::Proceed) {
      return step == Gate::Crash ? InstallError::Io
                                 : fail(InstallError::Io, "swap interrupted: " + relName(i));
    }
    if (!moveFile(staged, target, 0)) {
      return fail(InstallError::Io,
                  "cannot install " + relName(i) + " (" + errorText(::GetLastError()) + ")");
    }
    return m_journal.append("done " + std::to_string(i))
               ? InstallError::None
               : fail(InstallError::Io, "cannot write the journal");
  }

  InstallError commit() {
    const Gate step = gate("commit", 0);
    if (step == Gate::Crash) {
      return InstallError::Io;
    }
    if (step == Gate::Fail || !m_journal.append("committed")) {
      return rollBack(fail(InstallError::Io, "commit refused"));
    }
    if (gate("after-commit", 0) == Gate::Crash) {
      return InstallError::Io;
    }
    m_journal.close();
    if (!removeTree(m_paths.work, m_paths.root)) {
      m_detail = "installed; leftover work directory will be removed by recover";
    }
    return InstallError::None;
  }

  // Nothing was swapped yet: just remove the work directory.
  InstallError abandon(InstallError error) {
    m_journal.close();
    removeTree(m_paths.work, m_paths.root);
    return error;
  }

  InstallError rollBack(InstallError cause) {
    (void)cause;
    const std::string reason = m_detail;
    std::string failure;
    if (!rollbackAll(m_paths, m_state, failure)) {
      m_journal.close(); // keep journal and backup for recoverInstall
      m_detail = failure + " (after: " + reason + ")";
      return InstallError::RollbackFailed;
    }
    m_journal.close();
    removeTree(m_paths.work, m_paths.root);
    m_detail = "previous installation restored (" + reason + ")";
    return InstallError::RolledBack;
  }

  WorkPaths m_paths;
  const std::vector<PlannedFile> &m_plan;
  const InstallHooks *m_hooks;
  JournalState m_state;
  JournalWriter m_journal;
  std::string m_detail;
  bool m_crashed = false;
};

} // namespace

// --------------------------------------------------------------- layout

InstallTarget detectInstallTarget(const std::wstring &viewerExePath, std::string_view arch) {
  return guarded(
      [&] {
        InstallTarget target;
        target.arch = std::string(arch);
        target.root = parentDirectory(fullPath(viewerExePath));
        target.layout = fileExists(joinPath(target.root, L"LICENSE.txt")) ? InstallLayout::Full
                                                                          : InstallLayout::Standalone;
        return target;
      },
      InstallTarget{});
}

std::vector<PlannedFile> planInstall(const Manifest &manifest, const std::wstring &stagedFilesRoot,
                                     const InstallTarget &target) {
  return guarded(
      [&] {
        std::vector<PlannedFile> plan;
        for (const ManifestFile &file : manifest.files) {
          if (target.layout == InstallLayout::Standalone && !isTopLevelExecutable(file.path)) {
            continue;
          }
          PlannedFile planned;
          planned.relPath = utf8ToWide(file.path);
          std::replace(planned.relPath.begin(), planned.relPath.end(), L'/', L'\\');
          planned.source = joinPath(stagedFilesRoot, planned.relPath);
          planned.target = joinPath(target.root, planned.relPath);
          planned.size = file.size;
          planned.sha256 = file.sha256;
          plan.push_back(std::move(planned));
        }
        return plan;
      },
      std::vector<PlannedFile>{});
}

std::vector<std::wstring> viewerExecutables(const std::vector<PlannedFile> &plan) {
  return guarded(
      [&] {
        std::vector<std::wstring> executables;
        for (const PlannedFile &file : plan) {
          if (equalsNoCase(fileName(file.target), kViewerExeName)) {
            executables.push_back(file.target);
          }
        }
        return executables;
      },
      std::vector<std::wstring>{});
}

const char *installErrorName(InstallError error) {
  switch (error) {
  case InstallError::None:
    return "None";
  case InstallError::InUse:
    return "InUse";
  case InstallError::NoSpace:
    return "NoSpace";
  case InstallError::AccessDenied:
    return "AccessDenied";
  case InstallError::CopyFailed:
    return "CopyFailed";
  case InstallError::VerifyFailed:
    return "VerifyFailed";
  case InstallError::RolledBack:
    return "RolledBack";
  case InstallError::RollbackFailed:
    return "RollbackFailed";
  case InstallError::Io:
    return "Io";
  }
  return "Unknown";
}

// ------------------------------------------------------------ transaction

InstallError applyInstall(const InstallTarget &target, const std::vector<PlannedFile> &plan,
                          std::string *detail, const InstallHooks *hooks) {
  if (detail != nullptr) {
    *detail = "internal error";
  }
  return guarded(
      [&] {
        InstallRun run(target, plan, hooks);
        const InstallError error = run.run();
        if (detail != nullptr) {
          *detail = run.detail();
        }
        return error;
      },
      InstallError::Io);
}

bool hasPendingJournal(const std::wstring &root) {
  return guarded([&] { return fileExists(extended(WorkPaths(root).journal)); }, false);
}

InstallError recoverInstall(const std::wstring &root, std::string *detail) {
  if (detail != nullptr) {
    detail->clear();
  }
  const auto setDetail = [&](std::string text) {
    if (detail != nullptr) {
      *detail = std::move(text);
    }
  };
  return guarded(
      [&] {
        const WorkPaths paths(root);
        if (!hasPendingJournal(root)) {
          // A crash before the journal existed (or after it was removed): only
          // unswapped work files can be left.
          if (directoryExists(extended(paths.work)) && !removeTree(paths.work, root)) {
            setDetail("cannot remove the work directory");
            return InstallError::Io;
          }
          return InstallError::None;
        }
        const std::optional<std::string> text = readFile(extended(paths.journal), kMaxJournalBytes);
        if (!text) {
          setDetail("cannot read the journal");
          return InstallError::Io;
        }
        const JournalState state = parseJournal(*text);
        std::string failure;
        if (state.corrupt && !state.committed) {
          setDetail("journal is corrupt");
          return InstallError::RollbackFailed;
        }
        if (!state.committed && !rollbackAll(paths, state, failure)) {
          setDetail(failure);
          return InstallError::RollbackFailed;
        }
        if (!removeTree(paths.work, root)) {
          setDetail("cannot remove the work directory");
          return InstallError::Io;
        }
        return InstallError::None;
      },
      InstallError::Io);
}

} // namespace fsv::update
