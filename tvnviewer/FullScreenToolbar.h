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
// Modified for FarSightVNC on 2026-11-03: connection state label.
// Modified for FarSightVNC on 2026-11-11: rewritten as a floating pill drawn
// with ChromeRenderer; the buttons are the SessionActionStrip of the session
// toolbar.
//

#ifndef _FULL_SCREEN_TOOLBAR_H_
#define _FULL_SCREEN_TOOLBAR_H_

#include "util/CommonHeader.h"
#include "FullScreenToolbarLogic.h"
#include "shell/SessionActionStrip.h"

#include <string>

// Floating pill shown at the top center of a monitor while the session is in
// full screen, in the style of AnyDesk and RustDesk: the session name and the
// session actions of the windowed toolbar (same SessionActionStrip, same
// state) plus pin, leave full screen and disconnect. It is a layered
// (per-pixel alpha) topmost popup owned by the full screen shell that never
// takes the activation or the keyboard focus (WS_EX_NOACTIVATE,
// MA_NOACTIVATE); after a click the focus goes back to the remote desktop.
//
// FullScreenToolbarLogic decides when it shows: on entering full screen,
// when the pointer touches the top edge of a monitor or hovers it; it hides
// after a delay unless pinned. Hidden, it leaves a small grab handle at the
// top center of the monitor that brings it back when hovered.
class FullScreenToolbar : private SessionToolbarListener
{
public:
  FullScreenToolbar();
  ~FullScreenToolbar();

  // Creates the pill, owned by owner (the shell), and shows it on the
  // monitor until the auto-hide delay expires. Session commands are posted
  // to commandTarget and drop-downs go to listener; disconnect closes owner.
  // Does nothing if it is already created.
  void create(HWND owner, HWND commandTarget, SessionToolbarListener *listener,
              HWND focusTarget, HMONITOR monitor);
  void destroy();
  bool isCreated() const { return m_hwnd != 0; }
  HWND getHWnd() const { return m_hwnd; }

  // Shows the pill on the monitor (it moves there if needed).
  void showOn(HMONITOR monitor);
  // Pointer position over the remote desktop, in screen coordinates.
  void onPointerMove(POINT screenPoint);
  // Session state (kept while the pill does not exist).
  void setState(const SessionToolbarState &state);
  // Session name and connection state shown as small text.
  void setLabel(const TCHAR *text);

private:
  FullScreenToolbar(const FullScreenToolbar &);
  FullScreenToolbar &operator=(const FullScreenToolbar &);

  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  static bool registerClass();

  // Implementation of SessionToolbarListener: forwards the drop-downs.
  virtual void onToolbarMenu(SessionAction action, const RECT &anchor);

  // Places the window on the monitor, expanded (pill) or collapsed (handle).
  void layout(HMONITOR monitor);
  void render();
  void expand(HMONITOR monitor);
  void collapse();
  void onCommand(WPARAM wParam, LPARAM lParam);
  void applyState();
  bool isPointerNear() const;
  int nearMargin() const;
  FullScreenToolbarLogic::Box toolbarBox() const;

  static const UINT_PTR TIMER_AUTO_HIDE = 1;
  static const UINT TIMER_AUTO_HIDE_PERIOD = 100;

  HWND m_hwnd;
  HWND m_owner;
  HWND m_commandTarget;
  SessionToolbarListener *m_listener;
  HMONITOR m_monitor;
  bool m_inLayout;
  bool m_expanded;
  ChromeRenderer m_chrome;
  SessionActionStrip m_strip;
  FullScreenToolbarLogic m_logic;
  SessionToolbarState m_sessionState;
  std::wstring m_label;

  // Layout in DIPs, relative to the window.
  D2D1_RECT_F m_pill;
  D2D1_RECT_F m_labelRect;
  D2D1_RECT_F m_labelDivider;
};

#endif
