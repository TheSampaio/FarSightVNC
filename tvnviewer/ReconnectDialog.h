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

#ifndef _RECONNECT_DIALOG_H_
#define _RECONNECT_DIALOG_H_

// Only plain Windows types here: the implementation needs the Vista task
// dialog declarations, which the project-wide WINVER hides.
#include <windows.h>

// Modal "connection lost" prompt with Reconnect and Close buttons. With a
// countdown, Reconnect is clicked when it expires and a Stop retrying button
// stops it (the prompt stays open).
class ReconnectDialog
{
public:
  // Button ids (TDM_CLICK_BUTTON / BM_CLICK on the buttons work as well).
  static const int ID_RECONNECT = 1001;
  static const int ID_CLOSE = 1002;
  static const int ID_STOP = 1003;

  // owner may be 0. autoDelayMs == 0 shows no countdown. nextAttempt and
  // maxAttempts are only shown in the countdown text.
  // Returns true when the user (or the countdown) chose to reconnect.
  static bool show(HWND owner, const TCHAR *caption, const TCHAR *instruction,
                   const TCHAR *reason, unsigned int autoDelayMs,
                   unsigned int nextAttempt, unsigned int maxAttempts);
};

#endif
