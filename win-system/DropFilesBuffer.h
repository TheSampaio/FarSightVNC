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

#ifndef _DROP_FILES_BUFFER_H_
#define _DROP_FILES_BUFFER_H_

#include "util/CommonHeader.h"
#include "util/StringStorage.h"

#include <vector>

// The memory block of a CF_HDROP clipboard format: a DROPFILES header
// followed by the wide file names, each one terminated by a null character
// and the whole list terminated by a second null character.
//
// Both functions are plain memory work (no clipboard call, no file system
// access), so the layout can be tested on its own. DragQueryFile is not used:
// the format is simple enough to read directly and this keeps shell32 out of
// the dependencies of the server.
class DropFilesBuffer
{
public:
  // Builds the CF_HDROP block of the paths in 'buffer'. Always writes a
  // valid, double-null terminated block, also for an empty list.
  static void build(const std::vector<StringStorage> *paths,
                    std::vector<char> *buffer);

  // Reads the paths of a CF_HDROP block. Returns false when the block is too
  // small, when it is not a wide (fWide) list or when the name list is not
  // terminated inside the block; 'paths' then holds nothing. Empty names are
  // skipped.
  static bool parse(const void *buffer, size_t size,
                    std::vector<StringStorage> *paths);
};

#endif // _DROP_FILES_BUFFER_H_
