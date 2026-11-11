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

#ifndef _SESSION_ACTION_STRIP_H_
#define _SESSION_ACTION_STRIP_H_

#include "gui/d2d/ChromeRenderer.h"

#include <bitset>
#include <functional>
#include <memory>
#include <vector>

// Session command without a resource string: sends a Win key press to the
// server (the old toolbar and menu had none).
const int ID_SEND_WIN_KEY = 0xA850;
// Commands of the full screen pill, handled by the pill itself.
const int ID_PILL_PIN = 0xA860;
const int ID_PILL_DISCONNECT = 0xA870;

// Actions of the session toolbars, in their order on the bar. The table that
// maps them to commands, glyphs and tooltips is in SessionActionStrip.cpp
// (kActions); the icon phase swaps its glyphs for Lucide icons.
enum SessionAction
{
  // Display.
  SESSION_ACTION_FIT_WINDOW,
  SESSION_ACTION_SCALE_100,
  SESSION_ACTION_ZOOM_IN,
  SESSION_ACTION_ZOOM_OUT,
  SESSION_ACTION_FULL_SCREEN,   // Windowed bar only.
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
  // Full screen pill only.
  SESSION_ACTION_PIN,
  SESSION_ACTION_EXIT_FULL_SCREEN,
  SESSION_ACTION_DISCONNECT,

  SESSION_ACTION_COUNT
};

// Where an action is shown.
enum SessionActionPlacement
{
  SESSION_PLACEMENT_BAR = 1,   // Toolbar of the windowed session.
  SESSION_PLACEMENT_PILL = 2   // Floating pill of the full screen session.
};

// State of the session shown by the toolbars: which actions can be used and
// which toggles are on. The session computes it; the toolbars only draw it.
struct SessionToolbarState
{
  std::bitset<SESSION_ACTION_COUNT> enabled;
  std::bitset<SESSION_ACTION_COUNT> checked;

  bool operator==(const SessionToolbarState &other) const = default;
};

// Receives the drop-down buttons of a toolbar. Plain buttons post their
// WM_COMMAND to the command target instead, like a toolbar control.
class SessionToolbarListener
{
public:
  virtual ~SessionToolbarListener() {}
  // Shows the menu of a drop-down action below anchor (screen pixels).
  // The toolbar may be destroyed while the menu is open.
  virtual void onToolbarMenu(SessionAction action, const RECT &anchor) = 0;
};

// The row of session action buttons shared by the windowed toolbar and the
// full screen pill: layout in groups with hairline dividers, painting with
// ChromeRenderer and the light tokens, tooltips, hover/pressed tracking and
// clicks. The window that hosts it forwards its mouse messages; a click
// posts the command of the action to the command target (or opens the
// drop-down through the listener) and gives the keyboard focus back to the
// focus target (the remote desktop).
class SessionActionStrip
{
public:
  explicit SessionActionStrip(SessionActionPlacement placement);
  ~SessionActionStrip();

  // window draws the strip and receives its mouse messages; invalidate
  // redraws it. Call after the window and the renderer exist.
  void attach(HWND window, ChromeRenderer *chrome, HWND commandTarget,
              SessionToolbarListener *listener, std::function<void()> invalidate);
  // The window is being destroyed (WM_NCDESTROY).
  void detach();
  void setFocusTarget(HWND hwnd) { m_focusTarget = hwnd; }

  // Returns true if the state changed (the strip is then invalidated).
  bool setState(const SessionToolbarState &state);
  const SessionToolbarState &getState() const { return m_state; }

  // Places the buttons from left in DIPs, vertically centered between top
  // and bottom; a button that does not fit before right is not shown (the
  // overflow menu and the system menu still have every command). Returns the
  // right edge of the last button shown, or left.
  float layout(float left, float right, float top, float bottom);
  // Width of the strip when every button fits.
  static float fullWidth(SessionActionPlacement placement);

  void paint();

  // Handles the mouse messages of the window (move, leave, left button,
  // capture). Returns true when the message was handled.
  bool onMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
  // True while a drop-down menu of the strip is open.
  bool isMenuOpen() const { return m_menuOpen; }
  void restoreFocus();

private:
  SessionActionStrip(const SessionActionStrip &);
  SessionActionStrip &operator=(const SessionActionStrip &);

  bool isPlaced(int index) const;
  void paintButton(int index);
  void createTooltips();
  void updateTooltipRects();
  // Index of the action under a client point in pixels, or -1.
  int hitTest(LPARAM lParam) const;
  void setHover(int index);
  void trackMouseLeave();
  void onButtonDown(int index);
  void onButtonUp(int index);
  void invalidate();

  SessionActionPlacement m_placement;
  HWND m_hwnd;
  HWND m_tooltip;
  HWND m_commandTarget;
  HWND m_focusTarget;
  ChromeRenderer *m_chrome;
  SessionToolbarListener *m_listener;
  std::function<void()> m_invalidate;
  SessionToolbarState m_state;

  // Layout in DIPs: one rectangle per action (empty when it is not shown)
  // and the dividers between groups.
  D2D1_RECT_F m_buttons[SESSION_ACTION_COUNT];
  std::vector<D2D1_RECT_F> m_dividers;

  int m_hover;
  int m_pressed;
  bool m_trackingMouse;
  bool m_menuOpen;
  // Cleared by the destructor: a drop-down menu runs a modal loop in which
  // the session (and this strip) may be destroyed.
  std::shared_ptr<bool> m_alive;
};

#endif
