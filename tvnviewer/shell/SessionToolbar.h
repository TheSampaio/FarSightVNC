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

#ifndef _SESSION_TOOLBAR_H_
#define _SESSION_TOOLBAR_H_

#include "SessionActionStrip.h"

// Thin toolbar of a session, a child window drawn with ChromeRenderer and the
// light ThemeTokens: icon buttons in groups (display, input, transfer,
// session, overflow) and, on the right, the connection area (latency and
// security placeholders). The buttons are a SessionActionStrip, shared with
// the full screen pill. It never takes the activation nor the keyboard
// focus: after a click the focus goes back to the focus target (the remote
// desktop).
class SessionToolbar
{
public:
  SessionToolbar();
  ~SessionToolbar();

  // Creates the hidden toolbar as a child of parent. Commands are posted to
  // parent; drop-downs go to listener.
  bool create(HWND parent, SessionToolbarListener *listener);
  void destroy();
  HWND getHWnd() const { return m_hwnd; }

  // Window that takes the keyboard focus back after a click.
  void setFocusTarget(HWND hwnd) { m_strip.setFocusTarget(hwnd); }

  // Height of the bar in pixels at its DPI.
  int getHeight() const;
  void setDpi(UINT dpi);

  void setState(const SessionToolbarState &state);
  const SessionToolbarState &getState() const { return m_strip.getState(); }

  void show();
  void hide();
  bool isVisible() const;

private:
  SessionToolbar(const SessionToolbar &);
  SessionToolbar &operator=(const SessionToolbar &);

  static bool registerClass();
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);

  void layout();
  void paint();
  void invalidate();

  HWND m_hwnd;
  SessionToolbarListener *m_listener;
  ChromeRenderer m_chrome;
  SessionActionStrip m_strip;

  // Connection area in DIPs.
  D2D1_RECT_F m_latency;
  D2D1_RECT_F m_lock;
  bool m_showConnection;
};

#endif
