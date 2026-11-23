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
// Modified for FarSightVNC on 2026-11-12: "+" button (new connection, Lucide icon) right after the tab.
// Modified for FarSightVNC on 2026-11-22: any number of tabs, each with a close button; hit tests return the tab index.
//

#ifndef _TITLE_BAR_H_
#define _TITLE_BAR_H_

#include "TabStripLayout.h"
#include "gui/d2d/ChromeRenderer.h"

#include <string>
#include <vector>

// Parts of the title bar, for hit testing and the hover/pressed state.
enum TitleBarPart
{
  TITLE_BAR_NONE,
  TITLE_BAR_CAPTION,   // Empty space, logo: drags the window.
  TITLE_BAR_TAB,
  TITLE_BAR_TAB_CLOSE, // "x" button of a tab.
  TITLE_BAR_NEW_TAB,   // "+" button: a new Home tab.
  TITLE_BAR_SETTINGS,
  TITLE_BAR_MINIMIZE,
  TITLE_BAR_MAXIMIZE,
  TITLE_BAR_CLOSE
};

// Title bar drawn by the shell in the top of its client area (custom
// caption): logo, tab strip, settings button and the window buttons. It
// keeps the layout, the hover/pressed state and the titles of the tabs; the
// shell turns its parts into hit test codes and commands. Coordinates are in
// DIPs, relative to the client area.
class TitleBar
{
public:
  // No tab: the tab index of the parts that are not a tab.
  static constexpr size_t kNoTab = TabStripLayout::npos;

  TitleBar();

  // Height of the bar in DIPs.
  float height() const;

  // Lays the bar out for a client area width in DIPs.
  void layout(float width);
  // Part under the point. tab, when given, gets the index of the tab for
  // TITLE_BAR_TAB and TITLE_BAR_TAB_CLOSE, else kNoTab.
  TitleBarPart hitTest(float x, float y, size_t *tab = 0) const;
  // Rectangle of the "+" button (DIPs), for its tooltip.
  const D2D1_RECT_F &newTabRect() const { return m_tabs.newTabSlot(); }
  // Rectangle of a tab (DIPs; empty when it is not laid out) and the width
  // available to its title.
  const D2D1_RECT_F &tabRect(size_t index) const { return m_tabs.tab(index); }
  float tabLabelWidth(size_t index) const;
  size_t tabCount() const { return m_titles.size(); }
  const std::wstring &tabTitle(size_t index) const { return m_titles[index]; }

  // The tabs (their titles) and the active one; lays the strip out again.
  void setTabs(const std::vector<std::wstring> &titles, size_t active);
  // The Settings page is shown: the settings button looks selected.
  void setSettingsActive(bool active) { m_settingsActive = active; }

  // Hover and pressed parts (tab: the tab of a TITLE_BAR_TAB or
  // TITLE_BAR_TAB_CLOSE part); the setters return true when the state
  // changed (the bar must be repainted).
  bool setHover(TitleBarPart part, size_t tab = kNoTab);
  bool setPressed(TitleBarPart part, size_t tab = kNoTab);
  TitleBarPart hover() const { return m_hover; }
  TitleBarPart pressed() const { return m_pressed; }
  size_t hoverTab() const { return m_hoverTab; }
  size_t pressedTab() const { return m_pressedTab; }

  // Paints the whole bar. active: the window is active (inactive bars use
  // muted text); maximized selects the restore glyph.
  void paint(ChromeRenderer *renderer, bool active, bool maximized) const;

private:
  void paintCaptionButton(ChromeRenderer *renderer, TitleBarPart part, wchar_t icon,
                          const wchar_t *placeholder, bool active) const;
  void paintNewTabButton(ChromeRenderer *renderer, bool active) const;
  void paintTab(ChromeRenderer *renderer, size_t index, bool active, float line) const;
  // The label of a tab ends before its close button.
  D2D1_RECT_F tabLabelRect(size_t index) const;
  void layoutTabs();
  // Background of a button for its hover/pressed state, or false (none).
  bool buttonBackground(TitleBarPart part, ThemeColor *color) const;
  const D2D1_RECT_F &partRect(TitleBarPart part) const;

  float m_width;
  // Horizontal extent of the tab strip, from layout().
  float m_stripLeft;
  float m_stripRight;
  D2D1_RECT_F m_logo;
  TabStripLayout m_tabs;
  D2D1_RECT_F m_settings;
  D2D1_RECT_F m_minimize;
  D2D1_RECT_F m_maximize;
  D2D1_RECT_F m_close;
  D2D1_RECT_F m_empty;
  std::vector<std::wstring> m_titles;
  size_t m_activeTab;
  TitleBarPart m_hover;
  size_t m_hoverTab;
  TitleBarPart m_pressed;
  size_t m_pressedTab;
  bool m_settingsActive;
};

#endif
