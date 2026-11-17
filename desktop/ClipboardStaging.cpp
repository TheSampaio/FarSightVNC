// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a modified version of TightVNC.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Added for FarSightVNC on 2026-11-16: clipboard file paste (local viewer
// machine -> remote server machine).
//

#include "ClipboardStaging.h"

#include <vector>

namespace {

// Milliseconds since the Unix epoch of a FILETIME, 0 when it is not set.
UINT64 toUnixMillis(const FILETIME *time)
{
  ULARGE_INTEGER value;
  value.LowPart = time->dwLowDateTime;
  value.HighPart = time->dwHighDateTime;
  // 100 nanosecond intervals between 1601-01-01 and 1970-01-01.
  const UINT64 epochOffset = 116444736000000000ULL;
  if (value.QuadPart < epochOffset) {
    return 0;
  }
  return (value.QuadPart - epochOffset) / 10000ULL;
}

UINT64 nowUnixMillis()
{
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  return toUnixMillis(&now);
}

} // namespace

bool ClipboardStaging::getClipboardDir(StringStorage *dir)
{
  TCHAR tempPath[MAX_PATH + 1];
  DWORD length = GetTempPath(MAX_PATH, tempPath);
  if (length == 0 || length > MAX_PATH) {
    return false;
  }
  dir->setString(tempPath);
  if (!dir->endsWith(_T('\\'))) {
    dir->appendChar(_T('\\'));
  }
  dir->appendString(_T("FarSightVNC\\Clipboard"));
  return true;
}

bool ClipboardStaging::removeTree(const StringStorage *path)
{
  StringStorage pattern(path->getString());
  pattern.appendString(_T("\\*"));

  WIN32_FIND_DATA found;
  HANDLE search = FindFirstFile(pattern.getString(), &found);
  if (search != INVALID_HANDLE_VALUE) {
    do {
      if (_tcscmp(found.cFileName, _T(".")) == 0 ||
          _tcscmp(found.cFileName, _T("..")) == 0) {
        continue;
      }
      StringStorage child(path->getString());
      child.appendChar(_T('\\'));
      child.appendString(found.cFileName);
      if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
          (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        removeTree(&child);
      } else {
        SetFileAttributes(child.getString(), FILE_ATTRIBUTE_NORMAL);
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
          RemoveDirectory(child.getString()); // a reparse point, not its target
        } else {
          DeleteFile(child.getString());
        }
      }
    } while (FindNextFile(search, &found) != FALSE);
    FindClose(search);
  }
  return RemoveDirectory(path->getString()) != FALSE;
}

void ClipboardStaging::removeOldRoots(const StringStorage *clipboardDir,
                                      LogWriter *log)
{
  StringStorage pattern(clipboardDir->getString());
  pattern.appendString(_T("\\*"));

  WIN32_FIND_DATA found;
  HANDLE search = FindFirstFile(pattern.getString(), &found);
  if (search == INVALID_HANDLE_VALUE) {
    return;
  }

  UINT64 now = nowUnixMillis();
  // The names are collected first: removing while the search is open would
  // walk a directory that is being changed.
  std::vector<StringStorage> stale;
  do {
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        _tcscmp(found.cFileName, _T(".")) == 0 ||
        _tcscmp(found.cFileName, _T("..")) == 0) {
      continue;
    }
    UINT64 written = toUnixMillis(&found.ftLastWriteTime);
    if (written != 0 && now > written && now - written <= MAX_AGE_MS) {
      continue;
    }
    StringStorage path(clipboardDir->getString());
    path.appendChar(_T('\\'));
    path.appendString(found.cFileName);
    stale.push_back(path);
  } while (FindNextFile(search, &found) != FALSE);
  FindClose(search);

  for (size_t i = 0; i < stale.size(); i++) {
    if (removeTree(&stale[i])) {
      log->debug(_T("Removed the old clipboard staging folder %s"),
                 stale[i].getString());
    } else {
      log->debug(_T("Could not remove the old clipboard staging folder %s"),
                 stale[i].getString());
    }
  }
}

bool ClipboardStaging::createRoot(StringStorage *root, LogWriter *log)
{
  root->setString(_T(""));

  StringStorage clipboardDir;
  if (!getClipboardDir(&clipboardDir)) {
    log->error(_T("Clipboard file paste: the temp folder is not available"));
    return false;
  }

  // The two fixed levels may already exist; only a failure of the last
  // CreateDirectory matters.
  StringStorage productDir(clipboardDir.getString());
  size_t separator = productDir.findLast(_T('\\'));
  if (separator != (size_t)-1) {
    productDir.getSubstring(&productDir, 0, separator - 1);
    CreateDirectory(productDir.getString(), 0);
  }
  CreateDirectory(clipboardDir.getString(), 0);

  removeOldRoots(&clipboardDir, log);

  // The unique name holds only characters that
  // ClipboardFilesMessage::isStagingRoot accepts.
  for (int attempt = 0; attempt < 16; attempt++) {
    StringStorage candidate;
    candidate.format(_T("%s\\%08x%08x"), clipboardDir.getString(),
                     (unsigned)GetCurrentProcessId(),
                     (unsigned)(nowUnixMillis() + attempt));
    if (CreateDirectory(candidate.getString(), 0) != FALSE) {
      *root = candidate;
      log->debug(_T("Clipboard file paste: staging folder %s"),
                 root->getString());
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      break;
    }
  }

  log->error(_T("Clipboard file paste: could not create a staging folder")
             _T(" under %s"), clipboardDir.getString());
  return false;
}
