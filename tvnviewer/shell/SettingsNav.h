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

#ifndef _SETTINGS_NAV_H_
#define _SETTINGS_NAV_H_

#include "gui/d2d/ChromeRenderer.h"

#include <string>
#include <vector>

// Vertical list of items (glyph and label) drawn with ChromeRenderer: the
// section navigation of the Settings page, or a single button (Back). It is
// one tab stop. A list follows the selection with the arrows, Home and End
// and notifies its parent at once; a button runs on a click, Enter or Space.
// The parent receives WM_COMMAND with the control ID and NOTIFY_SELECT (the
// selected index is selected()) or NOTIFY_CLICK. The window text is the
// selected item, which screen readers announce.
class SettingsNav
{
public:
  enum Mode
  {
    MODE_LIST,
    MODE_BUTTON
  };
  // Notification codes (HIWORD of WM_COMMAND).
  static const UINT NOTIFY_SELECT = 1;
  static const UINT NOTIFY_CLICK = 2;

  struct Item
  {
    wchar_t glyph;      // Codepoint of the system icon font, 0 for none.
    std::wstring label;
  };

  SettingsNav(HWND parent, UINT controlId, Mode mode, const std::vector<Item> &items);
  ~SettingsNav();

  HWND getHWnd() const { return m_hwnd; }
  size_t selected() const { return m_selected; }
  void select(size_t index);
  void setDpi(UINT dpi);
  // Height in DIPs of all the items.
  float contentHeight() const;
  // Height in DIPs of one item and the gap below it.
  static float itemPitch();

private:
  SettingsNav(const SettingsNav &);
  SettingsNav &operator=(const SettingsNav &);

  static bool registerClass();
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  int hitTest(LPARAM lParam) const;
  D2D1_RECT_F itemRect(size_t index) const;
  void setHover(int item);
  void moveSelection(int index);
  void notify(UINT code);
  void paint();
  void invalidate();

  HWND m_hwnd;
  UINT m_id;
  Mode m_mode;
  std::vector<Item> m_items;
  size_t m_selected;
  int m_hover;
  int m_pressed;
  bool m_trackingMouse;
  ChromeRenderer m_chrome;
};

#endif
