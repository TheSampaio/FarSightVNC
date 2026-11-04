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
// Added for FarSightVNC on 2026-11-01: per-monitor DPI helpers.
//

#ifndef _DPI_UTILS_H_
#define _DPI_UTILS_H_

#include "util/CommonHeader.h"

// The project builds with WINVER 0x0501, which hides this message.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
// Access to the per-monitor DPI functions of user32.dll. They are loaded
// dynamically because Windows 10 releases before 1607 don't have them; the
// functions fall back to the system-wide values there.
class DpiUtils
{
public:
  static const UINT DEFAULT_DPI = 96;

  // Returns the DPI of the monitor the window is on.
  static UINT getDpiForWindow(HWND hwnd);
  // GetSystemMetrics() for the specified DPI.
  static int getSystemMetrics(int index, UINT dpi);
  // AdjustWindowRectEx() for the specified DPI, without menu.
  static bool adjustWindowRect(RECT *rect, DWORD style, DWORD exStyle, UINT dpi);
  // Converts a value given for DEFAULT_DPI to the specified DPI.
  static int scale(int value, UINT dpi);
};

#endif
