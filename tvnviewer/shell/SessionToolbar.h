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

#include "gui/d2d/ChromeRenderer.h"

#include <bitset>
#include <memory>
#include <vector>

// Session command without a resource string: sends a Win key press to the
// server (the old toolbar and menu had none).
const int ID_SEND_WIN_KEY = 0xA850;

// Actions of the session toolbar, in their order on the bar. The table that
// maps them to commands, glyphs and tooltips is in SessionToolbar.cpp
// (kActions); the icon phase swaps its glyphs for Lucide icons.
enum SessionAction
{
  // Display.
  SESSION_ACTION_FIT_WINDOW,
  SESSION_ACTION_SCALE_100,
  SESSION_ACTION_ZOOM_IN,
  SESSION_ACTION_ZOOM_OUT,
  SESSION_ACTION_FULL_SCREEN,
  SESSION_ACTION_MONITOR,       // Drop-down: full screen on a monitor.
  // Input.
  SESSION_ACTION_CTRL_ALT_DEL,
  SESSION_ACTION_WIN_KEY,
  SESSION_ACTION_CTRL_LATCH,
  SESSION_ACTION_ALT_LATCH,
  SESSION_ACTION_SPECIAL_KEYS,  // Drop-down: key combinations.
  // Transfer.
  SESSION_ACTION_FILE_TRANSFER,
  // Session.
  SESSION_ACTION_PAUSE,
  SESSION_ACTION_REFRESH,
  // Overflow ("..."): every other session command.
  SESSION_ACTION_MORE,

  SESSION_ACTION_COUNT
};

// State of the session shown by the toolbar: which actions can be used and
// which toggles are on. The session computes it; the toolbar only draws it.
struct SessionToolbarState
{
  std::bitset<SESSION_ACTION_COUNT> enabled;
  std::bitset<SESSION_ACTION_COUNT> checked;

  bool operator==(const SessionToolbarState &other) const = default;
};

// Receives the drop-down buttons of the toolbar. Plain buttons post their
// WM_COMMAND to the parent window instead, like a toolbar control.
class SessionToolbarListener
{
public:
  virtual ~SessionToolbarListener() {}
  // Shows the menu of a drop-down action below anchor (screen pixels).
  // The toolbar may be destroyed while the menu is open.
  virtual void onToolbarMenu(SessionAction action, const RECT &anchor) = 0;
};

// Thin toolbar of a session, a child window drawn with ChromeRenderer and the
// light ThemeTokens: icon buttons in groups (display, input, transfer,
// session, overflow) and, on the right, the connection area (latency and
// security placeholders). It never takes the activation nor the keyboard
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
  void setFocusTarget(HWND hwnd) { m_focusTarget = hwnd; }

  // Height of the bar in pixels at its DPI.
  int getHeight() const;
  void setDpi(UINT dpi);

  void setState(const SessionToolbarState &state);
  const SessionToolbarState &getState() const { return m_state; }

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
  void paintButton(size_t index);
  void createTooltips();
  void updateTooltipRects();
  // Index of the action under a client point in pixels, or -1.
  int hitTest(LPARAM lParam) const;
  void setHover(int index);
  void trackMouseLeave();
  void onButtonDown(int index);
  void onButtonUp(int index);
  void restoreFocus();
  void invalidate();

  HWND m_hwnd;
  HWND m_tooltip;
  HWND m_focusTarget;
  SessionToolbarListener *m_listener;
  ChromeRenderer m_chrome;
  SessionToolbarState m_state;

  // Layout in DIPs: one rectangle per action (empty when it does not fit),
  // the dividers between groups and the connection area.
  D2D1_RECT_F m_buttons[SESSION_ACTION_COUNT];
  std::vector<D2D1_RECT_F> m_dividers;
  D2D1_RECT_F m_latency;
  D2D1_RECT_F m_lock;
  bool m_showConnection;

  int m_hover;
  int m_pressed;
  bool m_trackingMouse;
  // Cleared by the destructor: a drop-down menu runs a modal loop in which
  // the session (and this toolbar) may be destroyed.
  std::shared_ptr<bool> m_alive;
};

#endif
