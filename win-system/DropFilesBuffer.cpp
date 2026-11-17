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

#include "DropFilesBuffer.h"

#include <shlobj.h>

// The whole product is built with the Unicode character set, so a file name
// of a StringStorage is already a wide string and the CF_HDROP list is always
// written with fWide set.
static_assert(sizeof(TCHAR) == sizeof(WCHAR),
              "DropFilesBuffer needs the Unicode character set");

void DropFilesBuffer::build(const std::vector<StringStorage> *paths,
                            std::vector<char> *buffer)
{
  // DROPFILES header, then every name with its null character, then the
  // second null character that ends the list.
  size_t charCount = 1;
  for (size_t i = 0; i < paths->size(); i++) {
    charCount += (*paths)[i].getLength() + 1;
  }

  buffer->assign(sizeof(DROPFILES) + charCount * sizeof(WCHAR), 0);

  DROPFILES *header = reinterpret_cast<DROPFILES *>(&buffer->front());
  header->pFiles = sizeof(DROPFILES); // offset of the name list
  header->pt.x = 0;
  header->pt.y = 0;
  header->fNC = FALSE;
  header->fWide = TRUE;

  WCHAR *names = reinterpret_cast<WCHAR *>(&buffer->front() + sizeof(DROPFILES));
  size_t position = 0;
  for (size_t i = 0; i < paths->size(); i++) {
    const StringStorage &path = (*paths)[i];
    size_t length = path.getLength();
    memcpy(&names[position], path.getString(), length * sizeof(WCHAR));
    position += length;
    names[position++] = L'\0';
  }
  names[position] = L'\0'; // end of the list
}

bool DropFilesBuffer::parse(const void *buffer, size_t size,
                            std::vector<StringStorage> *paths)
{
  paths->clear();

  if (buffer == 0 || size < sizeof(DROPFILES) + sizeof(WCHAR)) {
    return false;
  }
  const DROPFILES *header = static_cast<const DROPFILES *>(buffer);
  if (!header->fWide) {
    return false; // an ANSI name list is not produced by this product
  }
  if (header->pFiles < sizeof(DROPFILES) || header->pFiles >= size) {
    return false;
  }

  const char *bytes = static_cast<const char *>(buffer);
  // The name list must hold whole wide characters.
  size_t listBytes = size - header->pFiles;
  size_t listChars = listBytes / sizeof(WCHAR);
  if (listChars == 0) {
    return false;
  }
  const WCHAR *names = reinterpret_cast<const WCHAR *>(bytes + header->pFiles);

  size_t start = 0;
  bool terminated = false;
  for (size_t i = 0; i < listChars; i++) {
    if (names[i] != L'\0') {
      continue;
    }
    if (i == start) {
      terminated = true; // empty name: end of the list
      break;
    }
    paths->push_back(StringStorage(&names[start]));
    start = i + 1;
  }
  if (!terminated) {
    paths->clear();
    return false;
  }
  return true;
}
