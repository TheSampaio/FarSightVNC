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
//

#ifndef _FULL_SCREEN_TOOLBAR_H_
#define _FULL_SCREEN_TOOLBAR_H_

#include "util/CommonHeader.h"
#include "gui/ToolBar.h"
#include "FullScreenToolbarLogic.h"

#include <memory>

// Small floating toolbar shown at the top center of a monitor while the viewer
// is in full screen. It is a top-level popup owned by the viewer window that
// never takes the activation or the keyboard focus (WS_EX_NOACTIVATE,
// MA_NOACTIVATE); pointer input over it goes to it, not to the remote desktop.
// Button commands are posted to the owner as WM_COMMAND (the ids of the main
// toolbar) or WM_CLOSE (disconnect).
class FullScreenToolbar
{
public:
  static const TCHAR WINDOW_CLASS_NAME[];
  // Command ids of the buttons that have no main toolbar counterpart.
  static const int ID_PIN = 0xA850;
  static const int ID_DISCONNECT = 0xA860;
  // Text-only label with the connection state; it has no action.
  static const int ID_STATE = 0xA870;

  FullScreenToolbar();
  ~FullScreenToolbar();

  // Creates the window and shows it on the monitor until the auto-hide delay
  // expires. Does nothing if it is already created.
  void create(HWND owner, HMONITOR monitor);
  void destroy();
  bool isCreated() const { return m_hwnd != 0; }
  HWND getHWnd() const { return m_hwnd; }

  // Shows the toolbar on the monitor (it moves there if needed).
  void showOn(HMONITOR monitor);
  // Pointer position over the remote desktop, in screen coordinates.
  void onPointerMove(POINT screenPoint);
  void enableButton(int id, bool enable);
  // Sets the connection state label (copied, truncated to 31 characters).
  void setStateText(const TCHAR *text);

private:
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  bool registerClass();
  void createButtons();
  void layout(HMONITOR monitor);
  void applyDpi(UINT dpi);
  void hideNow();
  void onCommand(int id);
  bool isPointerNear() const;
  HWND toolbarControl() const;
  int nearMargin() const;
  FullScreenToolbarLogic::Box toolbarBox() const;
  const TCHAR *tooltipText(int id) const;

  static const UINT_PTR TIMER_AUTO_HIDE = 1;
  static const UINT TIMER_AUTO_HIDE_PERIOD = 100;

  HWND m_hwnd;
  HWND m_owner;
  HMONITOR m_monitor;
  UINT m_dpi;
  HFONT m_font;
  bool m_inLayout;
  std::unique_ptr<ToolBar> m_toolbar;
  FullScreenToolbarLogic m_logic;
  TCHAR m_stateText[32];
};

#endif
