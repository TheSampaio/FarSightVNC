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

#ifndef __WINDOWSFILENAME_H__
#define __WINDOWSFILENAME_H__

#include "util/StringStorage.h"

// One place for the question "may this name or this path be used on a Windows
// filesystem?". Every component of the product that receives a name or a path
// from the other end of the connection (clipboard file paste, file transfer,
// anything added later) must ask these functions instead of writing its own
// character test, so that a rule tightened here is tightened everywhere.
//
// None of these functions touch the filesystem except canonicalize() and
// isPathInsideDirectory(), which only call GetFullPathName(); nothing is
// created, opened, written or deleted.
namespace WindowsFileName
{
  /**
   * Tells whether a name refers to one of the old DOS devices instead of to a
   * file.
   *
   * Windows still resolves CON, PRN, AUX, NUL, COM1..COM9 and LPT1..LPT9 to a
   * device in every directory, and it does so no matter which extension
   * follows: "CON", "con", "CON.txt" and "con.tar.gz" all open the console
   * device. Creating such a "file" therefore never stores the bytes the
   * operator asked for; it writes to a device, which can block the thread, lose
   * the data or print it on a serial port. A remote peer that chooses the name
   * also chooses which device gets written to, so the name is refused.
   *
   * The comparison is case insensitive and applies to the part of the name
   * before the first '.'. Names that merely begin like a device, such as
   * "CONSOLE", and the non-existent devices "COM0" and "COM10" are ordinary
   * names and are not reserved.
   *
   * @param name the name to test, not necessarily zero terminated.
   * @param length the number of characters of name to look at.
   * @return true when the name is a reserved device name.
   */
  bool isReservedDosDeviceName(const TCHAR *name, size_t length);

  /**
   * Tells whether a single path component may be handed to the filesystem or
   * to the shell.
   *
   * The name is refused when:
   *   - it is empty, because an empty component means "the directory itself";
   *   - it is "." or "..", which are directory references and would move the
   *     operation to another directory than the one that was checked;
   *   - it holds a character below 0x20 or one of \ / : < > " | ? *. The two
   *     separators and ':' would turn one component into a path, a drive
   *     relative path or an NTFS alternate data stream; the wildcards and the
   *     remaining characters are rejected by the Win32 API or, worse,
   *     interpreted by the shell;
   *   - it ends in a space or a '.', because Windows silently strips those and
   *     resolves the name to a different file than the one that was checked;
   *   - it is a reserved device name (see isReservedDosDeviceName).
   *
   * @param name the component to test, not necessarily zero terminated.
   * @param length the number of characters of name to look at.
   * @return true when the component is safe to use.
   */
  bool isSafeNameComponent(const TCHAR *name, size_t length);

  /**
   * Turns a path into its full, absolute form with GetFullPathName(), which
   * resolves ".", ".." and a relative path against the current directory.
   *
   * A containment check is only meaningful on canonical paths: "C:\out\..\x"
   * and "C:\x" are the same file, and only the canonical form shows it. The
   * required buffer size is asked of Win32 first and then allocated, because a
   * fixed MAX_PATH buffer would silently truncate a long path into a shorter
   * one that names a different file. The length is asked again when the path
   * grows between the two calls (the current directory can change in another
   * thread).
   *
   * @param path the path to canonicalize.
   * @param out receives the canonical path; it is left untouched on failure.
   * @return false when path is empty or when the Win32 call fails.
   */
  bool canonicalize(const TCHAR *path, StringStorage *out);

  /**
   * Tells whether a path stays inside a directory: the check that stops
   * directory traversal.
   *
   * Both sides are canonicalized first, so a "..", a relative path or a short
   * name cannot climb out of the directory while still looking like a path
   * below it. When either side cannot be canonicalized the answer is false:
   * the check fails closed, because a path that cannot be resolved cannot be
   * proven to be inside.
   *
   * The comparison is case insensitive, because Windows filesystems are: a
   * candidate spelled "C:\OUT\a.txt" reaches the same file as "C:\out\a.txt"
   * and a case-only difference must not defeat the check.
   *
   * The candidate is inside only when the directory is followed by a separator
   * and by at least one more character. With directory "C:\out", "C:\out\a.txt"
   * and "C:\out\sub\a.txt" are inside, while "C:\outside\a.txt" (a prefix match
   * without a separator boundary), "C:\out" itself and "C:\" are not. A
   * directory that already ends in a separator is accepted.
   *
   * @param candidate the path to test.
   * @param directory the directory that must contain it.
   * @return true only when candidate is strictly below directory.
   */
  bool isPathInsideDirectory(const TCHAR *candidate, const TCHAR *directory);
}

#endif // __WINDOWSFILENAME_H__
