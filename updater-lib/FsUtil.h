// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Small file-system helpers shared by the viewer, fsvncupdater and tests.

#pragma once

#include "UpdateTypes.h"

namespace fsv::update {

std::wstring utf8ToWide(std::string_view text);
std::string wideToUtf8(std::wstring_view text);

std::wstring joinPath(const std::wstring &dir, const std::wstring &name);
std::wstring parentDirectory(const std::wstring &path); // without trailing '\'
std::wstring fileName(const std::wstring &path);
std::wstring fullPath(const std::wstring &path);        // GetFullPathNameW
bool pathsEqual(const std::wstring &a, const std::wstring &b); // full path, case-insensitive

bool fileExists(const std::wstring &path);
bool directoryExists(const std::wstring &path);
bool ensureDirectory(const std::wstring &path);       // creates intermediate dirs
// Recursively deletes `path`. Refuses (returns false) unless `path` is inside
// `mustBeUnder` (full-path prefix check), as a guard against deleting the
// wrong tree. Does not follow reparse points.
bool removeTree(const std::wstring &path, const std::wstring &mustBeUnder);

std::optional<std::uint64_t> freeSpaceBytes(const std::wstring &dir);

// True if the current token can create, write and delete a file in `dir`
// (real probe with a random name, not an ACL guess).
bool canWriteDirectory(const std::wstring &dir);
// True if `file` can be opened for write+delete by the current token (does
// not modify it). Missing file -> canWriteDirectory(parent).
bool canReplaceFile(const std::wstring &file);

// Writes the whole buffer to `path` via a temp file + MoveFileEx(REPLACE),
// flushing before the rename.
bool writeFileAtomic(const std::wstring &path, std::string_view data);
std::optional<std::string> readFile(const std::wstring &path, std::uint64_t maxBytes);

std::wstring currentModulePath();      // GetModuleFileNameW(nullptr), long-path safe
std::wstring localAppDataDirectory();  // FOLDERID_LocalAppData

// Per-installation state directory:
// %LOCALAPPDATA%\FarSightVNC\Updates\<first 16 hex of SHA-256(lower(full root))>
std::wstring updateStateDirectory(const std::wstring &installRoot);

// ProductVersion string from an executable's VERSIONINFO ("0.2.1a").
std::optional<Version> readExecutableVersion(const std::wstring &exePath);

} // namespace fsv::update
