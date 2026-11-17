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
// The shared rules for names and paths that come from a remote peer and are
// about to be handed to the Windows filesystem or to the shell.
//

#include "util/WindowsFileName.h"

#include <vector>

namespace {

// How often canonicalize() accepts that the path grew between the call that
// asks for the length and the call that fills the buffer. A path that keeps
// growing is refused rather than retried forever.
const int MAX_FULL_PATH_ATTEMPTS = 4;

TCHAR toLower(TCHAR c)
{
  if (c >= _T('A') && c <= _T('Z')) {
    return static_cast<TCHAR>(c - _T('A') + _T('a'));
  }
  return c;
}

bool isSeparator(TCHAR c)
{
  return c == _T('\\') || c == _T('/');
}

// Compares the first length characters of name with the zero terminated
// literal, which must be exactly length characters long.
bool regionEqualsNoCase(const TCHAR *name, size_t length, const TCHAR *literal)
{
  for (size_t i = 0; i < length; i++) {
    if (literal[i] == 0 || toLower(name[i]) != toLower(literal[i])) {
      return false;
    }
  }
  return literal[length] == 0;
}

} // namespace

bool WindowsFileName::isReservedDosDeviceName(const TCHAR *name, size_t length)
{
  if (name == 0 || length == 0) {
    return false;
  }
  // Only the part before the first '.' decides: "CON.txt" is the console.
  size_t stemLength = 0;
  while (stemLength < length && name[stemLength] != _T('.')) {
    stemLength++;
  }
  if (stemLength == 3) {
    static const TCHAR *const DEVICES[] = {
      _T("CON"), _T("PRN"), _T("AUX"), _T("NUL")
    };
    for (size_t i = 0; i < sizeof(DEVICES) / sizeof(DEVICES[0]); i++) {
      if (regionEqualsNoCase(name, stemLength, DEVICES[i])) {
        return true;
      }
    }
    return false;
  }
  if (stemLength == 4) {
    // COM1..COM9 and LPT1..LPT9 only: there is no COM0, and "COM10" is four
    // characters plus a '0', so it does not reach this branch at all.
    //
    // The superscript digits are accepted as digits on purpose: Windows maps
    // the Unicode superscript one, two and three to 1, 2 and 3 when it
    // resolves a device name, so "COM<superscript one>" reaches the same
    // device as "COM1".
    TCHAR digit = name[3];
    bool isDeviceDigit = (digit >= _T('1') && digit <= _T('9')) ||
                         digit == 0x00B9 || digit == 0x00B2 || digit == 0x00B3;
    if (!isDeviceDigit) {
      return false;
    }
    return regionEqualsNoCase(name, 3, _T("COM")) ||
           regionEqualsNoCase(name, 3, _T("LPT"));
  }
  // CONIN$ and CONOUT$ are the console input and output devices.
  if (stemLength == 6) {
    return regionEqualsNoCase(name, stemLength, _T("CONIN$"));
  }
  if (stemLength == 7) {
    return regionEqualsNoCase(name, stemLength, _T("CONOUT$"));
  }
  return false;
}

bool WindowsFileName::isSafeNameComponent(const TCHAR *name, size_t length)
{
  if (name == 0 || length == 0) {
    return false;
  }
  if (regionEqualsNoCase(name, length, _T(".")) ||
      regionEqualsNoCase(name, length, _T(".."))) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    TCHAR c = name[i];
    if (c < 0x20 || c == _T('\\') || c == _T('/') || c == _T(':') ||
        c == _T('<') || c == _T('>') || c == _T('"') || c == _T('|') ||
        c == _T('?') || c == _T('*')) {
      return false;
    }
  }
  // A trailing space or dot makes Windows resolve the name to another file.
  TCHAR last = name[length - 1];
  if (last == _T(' ') || last == _T('.')) {
    return false;
  }
  // A device name would write to a device instead of to a file.
  return !isReservedDosDeviceName(name, length);
}

bool WindowsFileName::canonicalize(const TCHAR *path, StringStorage *out)
{
  if (path == 0 || path[0] == 0) {
    return false;
  }
  // The first call only reports how much room the full path needs, including
  // the terminator; a fixed MAX_PATH buffer would truncate a long path into a
  // different path instead of failing.
  DWORD required = GetFullPathName(path, 0, 0, 0);
  if (required == 0) {
    return false;
  }
  std::vector<TCHAR> buffer;
  for (int attempt = 0; attempt < MAX_FULL_PATH_ATTEMPTS; attempt++) {
    buffer.resize(required);
    DWORD written = GetFullPathName(path, required, &buffer.front(), 0);
    if (written == 0) {
      return false;
    }
    if (written < required) {
      // On success the return value excludes the terminator.
      out->setString(&buffer.front());
      return true;
    }
    // The path grew between the two calls (the current directory can change in
    // another thread): written is again the required size, terminator included.
    required = written;
  }
  return false;
}

bool WindowsFileName::isPathInsideDirectory(const TCHAR *candidate,
                                            const TCHAR *directory)
{
  if (candidate == 0 || directory == 0 ||
      candidate[0] == 0 || directory[0] == 0) {
    return false;
  }
  StringStorage fullCandidate;
  StringStorage fullDirectory;
  // Fail closed: a path that cannot be resolved cannot be proven to be inside.
  if (!canonicalize(candidate, &fullCandidate) ||
      !canonicalize(directory, &fullDirectory)) {
    return false;
  }

  const TCHAR *candidatePath = fullCandidate.getString();
  const TCHAR *directoryPath = fullDirectory.getString();
  size_t candidateLength = fullCandidate.getLength();
  size_t directoryLength = fullDirectory.getLength();
  // A trailing separator ("C:\out\", and "C:\" after GetFullPathName) is not
  // part of the name of the directory.
  while (directoryLength > 0 && isSeparator(directoryPath[directoryLength - 1])) {
    directoryLength--;
  }
  if (directoryLength == 0) {
    return false;
  }
  // The directory itself is not inside itself, and one more character has to
  // follow the separator for the candidate to name something below it.
  if (candidateLength < directoryLength + 2) {
    return false;
  }
  // Windows filesystems are case insensitive, so a case-only difference names
  // the same file and must not defeat the check.
  for (size_t i = 0; i < directoryLength; i++) {
    if (toLower(candidatePath[i]) != toLower(directoryPath[i])) {
      return false;
    }
  }
  // Without this boundary "C:\outside\a.txt" would pass as a path below
  // "C:\out".
  return isSeparator(candidatePath[directoryLength]);
}
