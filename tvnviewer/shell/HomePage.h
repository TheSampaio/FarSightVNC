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

#ifndef _HOME_PAGE_H_
#define _HOME_PAGE_H_

#include "HomeActions.h"
#include "client-config-lib/ConnectionConfig.h"
#include "client-config-lib/ConnectionFavorites.h"
#include "gui/d2d/ChromeRenderer.h"

#include <memory>
#include <string>
#include <vector>

// Start screen of a shell window, in the style of AnyDesk and RustDesk: a
// centered column with the address field (a native borderless EDIT inside a
// drawn rounded box, for IME and accessibility), the Connect button, the
// connection options and listening mode actions, and the favorite and recent
// hosts (ConnectionFavorites, ConnectionHistory) drawn with ChromeRenderer.
//
// Keyboard: Tab and Shift+Tab move between the field, the buttons and the
// host list (one tab stop); the arrows, Home and End move in the list; Enter
// connects (field, Connect, a host) or runs the focused button, Space runs
// buttons too; the menu key and Shift+F10 open the menu of a host; Ctrl+N
// opens a new window on Home. The page scrolls with the mouse wheel when it
// is taller than the window.
class HomePage
{
public:
  // Creates the page as a child of shell.
  HomePage(HWND shell, HomeActions *actions);
  ~HomePage();

  HWND getHWnd() const { return m_hwnd; }
  // The address field.
  HWND focusHwnd() const { return m_edit; }

  void setDpi(UINT dpi);
  // Reloads the hosts and repaints (listening mode, favorites, history).
  void refresh();

private:
  HomePage(const HomePage &);
  HomePage &operator=(const HomePage &);

  // Focusable parts of the page, in tab order; the hosts follow.
  enum Item
  {
    ITEM_NONE = -1,
    ITEM_ADDRESS,
    ITEM_CONNECT,
    ITEM_OPTIONS,
    ITEM_LISTEN,
    ITEM_FIRST_HOST
  };

  struct HostRow
  {
    std::wstring host;     // As saved and connected to.
    std::wstring name;     // Host without the port.
    std::wstring address;  // Host and port.
    bool favorite;
    bool inHistory;
    D2D1_RECT_F rect;      // Layout in DIPs, page coordinates (unscrolled).
  };

  static bool registerClass();
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  // Keys of the page and of the field; true when handled.
  bool onKey(WPARAM key, bool fromEdit);

  void createEdit();
  void updateEditFont();
  void loadHosts();
  void layout();
  void placeEdit();
  void paint();
  void paintButton(int item, const wchar_t *label, wchar_t glyph);
  void paintHost(size_t index);
  void paintEmptyState();

  // Rectangle of an item in DIPs, page coordinates (unscrolled).
  D2D1_RECT_F itemRect(int item) const;
  bool isFocusable(int item) const;
  int hitTest(LPARAM lParam) const;
  void setHover(int item);
  void setFocusItem(int item);
  void moveFocus(bool forward);
  void moveInHosts(int delta);
  void ensureVisible(int item);
  void scrollTo(float offset);
  void activate(int item);
  void connectFromField();
  void connect(const std::wstring &host, const ConnectionConfig &config);
  void showOptions();
  void showHostMenu(size_t index, const POINT *screenPoint);
  std::wstring fieldText() const;
  // Theme::changedMessage(): the GDI brush and the native field follow the
  // new palette, the drawn part is painted again.
  void onThemeChanged();
  void invalidate();

  HWND m_hwnd;
  HWND m_shell;
  HWND m_edit;
  WNDPROC m_editBaseProc;
  HFONT m_editFont;
  HBRUSH m_fieldBrush;
  HomeActions *m_actions;
  ChromeRenderer m_chrome;

  ConnectionFavorites m_favorites;
  std::vector<HostRow> m_hosts;
  size_t m_favoriteCount;
  // Options set with "Connection options..." for the next connection typed
  // in the field; otherwise the options saved for the host are used.
  ConnectionConfig m_connectionConfig;
  bool m_optionsEdited;

  // Layout in DIPs, page coordinates.
  D2D1_RECT_F m_heading;
  D2D1_RECT_F m_subtitle;
  D2D1_RECT_F m_field;
  D2D1_RECT_F m_connect;
  D2D1_RECT_F m_options;
  D2D1_RECT_F m_listen;
  D2D1_RECT_F m_listenStatus;
  D2D1_RECT_F m_favoritesHeader;
  D2D1_RECT_F m_recentsHeader;
  D2D1_RECT_F m_empty;
  float m_contentHeight;
  float m_scroll;

  int m_hover;
  int m_pressed;
  int m_focus;
  // Host that has the tab stop of the list.
  size_t m_hostFocus;
  bool m_trackingMouse;
  // A connection was asked for: the session replaces the page soon.
  bool m_connecting;
  // Cleared by the destructor: the menu of a host runs a modal loop.
  std::shared_ptr<bool> m_alive;
};

#endif
