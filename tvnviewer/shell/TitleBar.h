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

#ifndef _TITLE_BAR_H_
#define _TITLE_BAR_H_

#include "gui/d2d/ChromeRenderer.h"

#include <string>
#include <vector>

// Parts of the title bar, for hit testing and the hover/pressed state.
enum TitleBarPart
{
  TITLE_BAR_NONE,
  TITLE_BAR_CAPTION,   // Empty space, logo: drags the window.
  TITLE_BAR_TAB,
  TITLE_BAR_NEW_TAB,   // Reserved slot of the future "+" button.
  TITLE_BAR_SETTINGS,
  TITLE_BAR_MINIMIZE,
  TITLE_BAR_MAXIMIZE,
  TITLE_BAR_CLOSE
};

// Places the tabs of the title bar side by side, like the tabs of a browser:
// each one as wide as possible up to a maximum, all shrinking together when
// the strip is short, followed by the slot of the "+" button. Coordinates in
// DIPs.
class TabStripLayout
{
public:
  TabStripLayout();

  // Lays count tabs out between left and right, from top to bottom.
  void layout(float left, float right, float top, float bottom, size_t count);

  size_t count() const { return m_tabs.size(); }
  const D2D1_RECT_F &tab(size_t index) const { return m_tabs[index]; }
  const D2D1_RECT_F &newTabSlot() const { return m_newTab; }

private:
  std::vector<D2D1_RECT_F> m_tabs;
  D2D1_RECT_F m_newTab;
};

// Title bar drawn by the shell in the top of its client area (custom
// caption): logo, tab strip, settings button and the window buttons. It
// keeps the layout, the hover/pressed state and the title of the tab; the
// shell turns its parts into hit test codes and commands. Coordinates are in
// DIPs, relative to the client area.
class TitleBar
{
public:
  TitleBar();

  // Height of the bar in DIPs.
  float height() const;

  // Lays the bar out for a client area width in DIPs.
  void layout(float width);
  TitleBarPart hitTest(float x, float y) const;

  void setTitle(const std::wstring &title);
  // The Settings page is shown: the settings button looks selected.
  void setSettingsActive(bool active) { m_settingsActive = active; }

  // Hover and pressed parts; the setters return true when the state changed
  // (the bar must be repainted).
  bool setHover(TitleBarPart part);
  bool setPressed(TitleBarPart part);
  TitleBarPart hover() const { return m_hover; }
  TitleBarPart pressed() const { return m_pressed; }

  // Paints the whole bar. active: the window is active (inactive bars use
  // muted text); maximized selects the restore glyph.
  void paint(ChromeRenderer *renderer, bool active, bool maximized) const;

private:
  void paintCaptionButton(ChromeRenderer *renderer, TitleBarPart part, wchar_t icon,
                          const wchar_t *placeholder, bool active) const;
  // Background of a button for its hover/pressed state, or false (none).
  bool buttonBackground(TitleBarPart part, ThemeColor *color) const;
  const D2D1_RECT_F &partRect(TitleBarPart part) const;

  float m_width;
  D2D1_RECT_F m_logo;
  TabStripLayout m_tabs;
  D2D1_RECT_F m_settings;
  D2D1_RECT_F m_minimize;
  D2D1_RECT_F m_maximize;
  D2D1_RECT_F m_close;
  D2D1_RECT_F m_empty;
  std::wstring m_title;
  TitleBarPart m_hover;
  TitleBarPart m_pressed;
  bool m_settingsActive;
};

#endif
