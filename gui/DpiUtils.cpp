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

#include "DpiUtils.h"

typedef UINT (WINAPI *GetDpiForWindowFunc)(HWND);
typedef int (WINAPI *GetSystemMetricsForDpiFunc)(int, UINT);
typedef BOOL (WINAPI *AdjustWindowRectExForDpiFunc)(LPRECT, DWORD, BOOL, DWORD, UINT);

static FARPROC getUser32Function(const char *name)
{
  return GetProcAddress(GetModuleHandle(_T("user32.dll")), name);
}

UINT DpiUtils::getDpiForWindow(HWND hwnd)
{
  static GetDpiForWindowFunc function =
    reinterpret_cast<GetDpiForWindowFunc>(getUser32Function("GetDpiForWindow"));
  if (function != 0) {
    UINT dpi = function(hwnd);
    if (dpi != 0) {
      return dpi;
    }
  }
  HDC hdc = GetDC(hwnd);
  UINT dpi = hdc != 0 ? GetDeviceCaps(hdc, LOGPIXELSY) : DEFAULT_DPI;
  ReleaseDC(hwnd, hdc);
  return dpi;
}

int DpiUtils::getSystemMetrics(int index, UINT dpi)
{
  static GetSystemMetricsForDpiFunc function =
    reinterpret_cast<GetSystemMetricsForDpiFunc>(getUser32Function("GetSystemMetricsForDpi"));
  return function != 0 ? function(index, dpi) : GetSystemMetrics(index);
}

bool DpiUtils::adjustWindowRect(RECT *rect, DWORD style, DWORD exStyle, UINT dpi)
{
  static AdjustWindowRectExForDpiFunc function =
    reinterpret_cast<AdjustWindowRectExForDpiFunc>(getUser32Function("AdjustWindowRectExForDpi"));
  if (function != 0) {
    return !!function(rect, style, FALSE, exStyle, dpi);
  }
  return !!AdjustWindowRectEx(rect, style, FALSE, exStyle);
}

int DpiUtils::scale(int value, UINT dpi)
{
  return MulDiv(value, dpi, DEFAULT_DPI);
}
