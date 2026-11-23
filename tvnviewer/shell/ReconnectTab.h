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

#ifndef _RECONNECT_TAB_H_
#define _RECONNECT_TAB_H_

#include "ShellTab.h"

// Stands for a session that ended to be reconnected: it keeps the place and
// the titles of the tab until the application shows the next session there
// (a moment later). It has no view.
class ReconnectTab : public ShellTab
{
public:
  virtual HWND viewHwnd() const { return 0; }
  virtual HWND focusHwnd() const { return 0; }
  virtual bool onCommand(WPARAM, LPARAM) { return false; }
  virtual void onActivate(WPARAM) {}
  virtual void onFrameSizing() {}
  virtual void onDisplayChange() {}
  virtual void onDpiChanged(UINT, bool) {}
};

#endif
