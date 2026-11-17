// Copyright (C) 2012 GlavSoft LLC.
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

#ifndef _WIN_CLIPBOARD_H_
#define _WIN_CLIPBOARD_H_

// Modified for FarSightVNC on 2026-11-16: reading the CF_HDROP file list of
// the local clipboard (clipboard file paste).

#include "util/CommonHeader.h"

#include <vector>

class WinClipboard
{
public:

  WinClipboard(HWND hwnd);
  virtual ~WinClipboard();

  void setHWnd(HWND hwnd);
  
  // get string from windows clipboard
  bool getString(StringStorage *str);

  // update windows clipboard
  bool setString(const StringStorage *str);

  // True when the clipboard holds a file list (CF_HDROP), which is what
  // Explorer puts there when files are copied. Does not open the clipboard.
  bool hasFileList() const;

  // Reads the file list (CF_HDROP) of the clipboard. Returns false when the
  // clipboard holds no file list or could not be read; 'paths' then holds
  // nothing. The paths are the absolute local paths of the copied files and
  // folders, exactly as Explorer wrote them.
  bool getFileList(std::vector<StringStorage> *paths);

protected:
  static const TCHAR CR = _T('\r');
  static const TCHAR LF = _T('\n');

protected:
  // function removed CR before LF
  StringStorage removeCR(const StringStorage *str);

  // function replaced LF to CR+LF. If before LF already is CR, this not added second
  StringStorage addCR(const StringStorage *str);

  HANDLE m_hndClipboard;
  HWND m_hWnd;
};

#endif
