// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Process and in-use checks declared in Installer.h.

#include "Installer.h"

#include "FsUtil.h"
#include "Win32Util.h"

#include <tlhelp32.h>

#include <algorithm>

namespace fsv::update {

namespace {

constexpr DWORD kMaxWaitHandles = MAXIMUM_WAIT_OBJECTS; // 64

bool sameFileName(const wchar_t *processName, const std::wstring &name) {
  const std::wstring_view a = processName;
  return a.size() == name.size() &&
         ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), name.data(),
                                static_cast<int>(name.size()), TRUE) == CSTR_EQUAL;
}

std::wstring processImagePath(DWORD pid) {
  UniqueHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process.valid()) {
    return {};
  }
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    DWORD size = static_cast<DWORD>(buffer.size());
    if (::QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &size)) {
      buffer.resize(size);
      return buffer;
    }
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 65536) {
      return {};
    }
    buffer.resize(buffer.size() * 2);
  }
}

bool matchesAny(const std::wstring &imagePath, const std::vector<std::wstring> &exePaths) {
  return std::any_of(exePaths.begin(), exePaths.end(),
                     [&](const std::wstring &exe) { return pathsEqual(imagePath, exe); });
}

} // namespace

std::vector<std::uint32_t> findProcessesByImage(const std::vector<std::wstring> &exePaths,
                                                std::uint32_t excludePid) {
  return guarded(
      [&] {
        std::vector<std::uint32_t> found;
        if (exePaths.empty()) {
          return found;
        }
        std::vector<std::wstring> names;
        for (const std::wstring &exe : exePaths) {
          names.push_back(fileName(exe));
        }
        UniqueHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snapshot.valid()) {
          return found;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = ::Process32FirstW(snapshot.get(), &entry); more;
             more = ::Process32NextW(snapshot.get(), &entry)) {
          if (entry.th32ProcessID == 0 || entry.th32ProcessID == excludePid ||
              std::none_of(names.begin(), names.end(), [&](const std::wstring &name) {
                return sameFileName(entry.szExeFile, name);
              })) {
            continue; // cheap name filter first; the full path decides below
          }
          const std::wstring image = processImagePath(entry.th32ProcessID);
          if (!image.empty() && matchesAny(image, exePaths)) {
            found.push_back(entry.th32ProcessID);
          }
        }
        return found;
      },
      std::vector<std::uint32_t>{});
}

bool waitForProcessesToExit(const std::vector<std::wstring> &exePaths,
                            const std::function<bool()> &cancelled, std::uint32_t pollMs) {
  return guarded(
      [&] {
        for (;;) {
          const std::vector<std::uint32_t> pids = findProcessesByImage(exePaths);
          if (pids.empty()) {
            return true;
          }
          if (cancelled && cancelled()) {
            return false;
          }
          std::vector<UniqueHandle> owned;
          std::vector<HANDLE> handles;
          for (const std::uint32_t pid : pids) {
            if (handles.size() >= kMaxWaitHandles) {
              break;
            }
            UniqueHandle process(::OpenProcess(SYNCHRONIZE, FALSE, pid));
            if (process.valid()) {
              handles.push_back(process.get());
              owned.push_back(std::move(process));
            }
          }
          // Wake when any of them exits, or after pollMs to re-scan (this also
          // catches instances started meanwhile and re-checks `cancelled`).
          if (handles.empty()) {
            ::Sleep(pollMs);
          } else {
            ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                     pollMs);
          }
        }
      },
      false);
}

bool isFileInUse(const std::wstring &path) {
  return guarded(
      [&] {
        if (!fileExists(path)) {
          return false;
        }
        UniqueHandle file(::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.valid()) {
          return false;
        }
        const DWORD error = ::GetLastError();
        return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
      },
      false);
}

} // namespace fsv::update
