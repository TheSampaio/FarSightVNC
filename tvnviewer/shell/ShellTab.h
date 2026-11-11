// Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
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

#ifndef _SHELL_TAB_H_
#define _SHELL_TAB_H_

#include "util/winhdr.h"

// Content of one tab of the shell window. The shell lays the tab's view out
// in its content area and forwards to the active tab the frame events that
// only a top-level window receives.
class ShellTab
{
public:
  virtual ~ShellTab() {}

  // Child window of the shell that shows the tab.
  virtual HWND viewHwnd() const = 0;
  // Window that gets the keyboard focus when the shell is activated.
  virtual HWND focusHwnd() const = 0;

  // WM_COMMAND and WM_SYSCOMMAND of the shell (menus, accelerators, the full
  // screen toolbar). Returns true when the tab handled the command.
  virtual bool onCommand(WPARAM wParam, LPARAM lParam) = 0;
  // WM_ACTIVATE of the shell.
  virtual void onActivate(WPARAM wParam) = 0;
  // The user is resizing the shell (WM_SIZING).
  virtual void onFrameSizing() = 0;
  // WM_DISPLAYCHANGE of the shell.
  virtual void onDisplayChange() = 0;
  // WM_DPICHANGED of the shell, before the shell takes the rectangle
  // suggested by the system (frameMoved) or keeps its full screen size.
  virtual void onDpiChanged(UINT dpi, bool frameMoved) = 0;
};

#endif
