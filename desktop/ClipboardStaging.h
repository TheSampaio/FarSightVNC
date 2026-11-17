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

#ifndef __CLIPBOARDSTAGING_H__
#define __CLIPBOARDSTAGING_H__

#include "util/CommonHeader.h"
#include "log-writer/LogWriter.h"

// The per-paste staging folders of the clipboard file paste, under
// "%TEMP%\FarSightVNC\Clipboard\<unique>".
//
// It runs in the session of the interactive user (the desktop server helper
// process, or the server process itself in application mode), so %TEMP% is
// the temp folder of that user and the folder it creates is owned by them.
// The viewer then uploads the copied files into it through the ordinary file
// transfer, which the server performs impersonated as the same user.
class ClipboardStaging
{
public:
  // A staging folder is removed once nothing can be pasting from it any
  // more. One hour after its last write is far longer than a paste in
  // Explorer takes and short enough to bound the temp folder.
  static const UINT64 MAX_AGE_MS = 60 * 60 * 1000;

  // Creates a new staging folder and puts its absolute path in 'root'.
  // Before that it removes the staging folders of earlier pastes that are
  // older than MAX_AGE_MS, so that a session which pastes repeatedly cleans
  // up after itself and a crashed session leaks at most one folder until the
  // next paste. Returns false (and leaves 'root' empty) when the folder
  // could not be created.
  static bool createRoot(StringStorage *root, LogWriter *log);

private:
  // "%TEMP%\FarSightVNC\Clipboard", without a trailing separator.
  static bool getClipboardDir(StringStorage *dir);
  static void removeOldRoots(const StringStorage *clipboardDir, LogWriter *log);
  // Removes a folder and everything in it.
  static bool removeTree(const StringStorage *path);
};

#endif // __CLIPBOARDSTAGING_H__
