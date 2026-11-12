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

#ifndef _SESSION_HOST_H_
#define _SESSION_HOST_H_

#include "SettingsSection.h"
#include "util/winhdr.h"

class StringStorage;

// Everything a session (ViewerWindow) needs from the top-level frame that
// shows it: the shell window (ShellWindow), which hosts the session view as
// a child window.
class SessionHost
{
public:
  virtual ~SessionHost() {}

  // Window that contains the session view (its parent in the shell).
  virtual HWND hostHwnd() const = 0;
  // Top-level window of the session: foreground and activation checks,
  // owner of the session popups.
  virtual HWND rootHwnd() const = 0;

  // Shows the frame (the session is ready or a reconnection is shown).
  virtual void showFrame() = 0;
  // windowTitle names the frame (taskbar, Alt+Tab); tabTitle, the session
  // name alone, is shown in the tab of the session.
  virtual void setTitle(const StringStorage *windowTitle, const StringStorage *tabTitle) = 0;

  // Covers monitorRect (screen coordinates, one monitor or the virtual
  // screen) with a borderless topmost frame. Called again while in full
  // screen to move to other monitor(s); the windowed placement is saved
  // only on the first call.
  virtual void enterFullscreen(const RECT &monitorRect) = 0;
  // Restores the windowed frame saved by enterFullscreen().
  virtual void exitFullscreen() = 0;
  virtual bool isFullscreen() const = 0;

  // Fits the windowed frame to a session client area of width x height
  // pixels, centered in the work area of its monitor; when it does not fit,
  // the frame covers the work area. Does nothing if that size was already
  // applied.
  virtual void requestClientSize(int width, int height) = 0;
  // A windowed session is shown: a new frame opens maximized, a frame that
  // already showed a session (reconnection) keeps its size.
  virtual void openMaximized() = 0;

  // The session view is being torn down (closed, disconnected or failed).
  // The view is destroyed; the frame is destroyed too unless the session is
  // reconnecting, in which case the next session is shown in the same frame.
  virtual void onSessionEnded(bool reconnecting) = 0;

  // Shows the Settings page of the frame on section, over the session.
  virtual void openSettings(SettingsSectionId section) = 0;
};

#endif
