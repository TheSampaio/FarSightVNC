// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
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
//-------------------------------------------------------------------------
//

#ifndef _FILE_H_
#define _FILE_H_

#include "util/CommonHeader.h"
#include "util/inttypes.h"

class File
{
public:

  const static int MODE_READ  = 1;
  const static int MODE_WRITE = 2;

  File(const TCHAR *pathname);
  File(const TCHAR *parent, const TCHAR *child);
  ~File();

  //
  // Tests whether the application can read the file denoted by this pathname
  //

  bool canRead() const;

  //
  // Tests whether the application can read the file denoted by this pathname
  //

  bool canWrite() const;

  //
  // Atomically creates a new, empty file named by this pathname
  // if and only if a file with this name does not yet exist.
  //

  bool createNewFile() const;

  //
  // Deletes the file or directory denoted by this pathname
  //

  bool remove() const;

  //
  // Tests whether the file or directory denoted by this pathname exists
  //

  bool exists() const;

  /**
  Returns the name of the file or directory denoted by this pathname
  (not full path to file, only filename).
  @param name out parameter for file name.
  */
  void getName(StringStorage *name) const;

  /**
  Returns extension of file denoted by this pathname (without "." character).
  @param ext out parameter for file extension.
  @remark if filename has no extension, then ext will contain empty string("").
  */
  void getFileExtension(StringStorage *ext) const;

  /**
  Tests whether the file denoted by this pathname is a normal file.
  @return true if file is a normal file (not directory), false otherwise.
  */
  bool isFile() const;

  /**
  Tests whether the file denoted by this abstract pathname is a directory.
  @return true if this file is directory, false otherwise.
  */
  bool isDirectory() const;

  /**
  Tests whether the file or directory denoted by this pathname is a reparse
  point: a symbolic link, a junction or a mount point.

  Opening such a path reaches the file it points at, which is chosen by
  whoever created the link and not by the caller. Code that is about to write
  to a path it checked must therefore ask this first and refuse, because the
  check it made was about the link, not about the file that receives the bytes.

  @return true if this path is a reparse point. False when the path does not
  exist, so a caller must not read this as "it is safe to create".
  */
  bool isReparsePoint() const;

  /**
  Returns full path to file or directory.
  @param pathname out parameter for path to file or directory.
  */
  void getPath(StringStorage *pathname) const;

  /**
   * Returns the last modification time of file denoted by this pathname.
   * @return count of milliseconds since unix epoch or 0 on fail.
   * @fixme raise exception on fail.
   */
  UINT64 lastModified() const;

  /**
   * Returns the length of the file (in bytes) denoted by this pathname.
   * @fixme raise exception on fail.
   */
  UINT64 length() const;

  //
  // Fills fileList array of strings naming the files and directories
  // in the directory denoted by this pathname
  //
  // If fileList is NULL than sets files count value to filesCount
  //
  // FIXME: Current implementation forces to perform two calls, first to
  //        determine the list size, then to fill in the list itself.
  //        However, the list size can be changed between these calls!
  //        This MUST be fixed someday.
  //

  bool list(StringStorage *fileList, UINT32 *filesCount) const;

  //
  // List the available filesystem roots to rootList array
  //
  // If rootList is NULL that sets root list size to rootsCount
  //
  // FIXME: Current implementation forces to perform two calls, first to
  //        determine the list size, then to fill in the list itself.
  //        However, the list size can be changed between these calls!
  //        This MUST be fixed someday.
  //

  static bool listRoots(StringStorage *rootList, UINT32 *rootsCount);

  //
  // Creates the directory named by this pathname
  //

  bool mkdir() const;

  //
  // Renames the file denoted by this pathname.
  // Really this methods call move method.
  //

  bool renameTo(const TCHAR *destPathName);
  bool renameTo(File *dest);
  static bool renameTo(const TCHAR *dest, const TCHAR *source);

  /**
   * Sets the last-modified time of the file or directory named by this pathname.
   * @param time count of milliseconds since unix epoch.
   * @return true if ok, false if fail.
   */

  bool setLastModified(INT64 time);

  //
  // Sets the length of the file named by this pathname to zero, creating it
  // when it does not exist.
  //
  // The file is emptied in place: it is never deleted and recreated, because
  // deleting it destroys its contents before it is known that it can be
  // created again, and in the window between the two calls another process, or
  // a link planted at the path, decides what the name refers to. A reparse
  // point at the path is refused rather than followed.
  //
  // @return false when the file could not be emptied; its contents are then
  // left as they were, and the caller must not write to it.
  //

  bool truncate();

private:

  //
  // Helper methods to minimize code of File class
  //

  bool getFileInfo(WIN32_FIND_DATA *fileInfo) const;
  bool tryCreateFile(DWORD desiredAccess, DWORD creationDisposition) const;

public:
  static TCHAR s_separatorChar;

protected:
  StringStorage m_pathName;
};

#endif
