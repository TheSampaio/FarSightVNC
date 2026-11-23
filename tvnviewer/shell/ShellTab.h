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
// Modified for FarSightVNC on 2026-11-23: a tab keeps its titles and knows whether the user is closing it; isHome(), sessionMenu().
//

#ifndef _SHELL_TAB_H_
#define _SHELL_TAB_H_

#include "util/StringStorage.h"
#include "util/winhdr.h"

#include <string>

class SessionTab;

// Content of one tab of the shell window. The shell lays the tab's view out
// in its content area and forwards to the active tab the frame events that
// only a top-level window receives.
class ShellTab
{
public:
  ShellTab() : m_id(0), m_closing(false) {}
  virtual ~ShellTab() {}

  // Identity of the tab in its shell, given by the shell when it creates the
  // tab and never reused there; 0 until then.
  unsigned id() const { return m_id; }
  void setId(unsigned id) { m_id = id; }
  // The tab as a session tab, or 0 when it is not one.
  virtual SessionTab *asSession() { return 0; }
  // The tab is the Home page.
  virtual bool isHome() const { return false; }

  // Titles of the tab: windowTitle names the frame while the tab is the
  // active one (taskbar, Alt+Tab), tabTitle is the label in the title bar.
  const StringStorage &windowTitle() const { return m_windowTitle; }
  const std::wstring &tabTitle() const { return m_tabTitle; }
  void setTitles(const StringStorage &windowTitle, const std::wstring &tabTitle)
  {
    m_windowTitle = windowTitle;
    m_tabTitle = tabTitle;
  }

  // The user asked to close the tab and its session is closing: when it ends
  // the tab goes away instead of turning into a Home tab.
  bool isClosing() const { return m_closing; }
  void setClosing(bool closing) { m_closing = closing; }

  // Menu with the commands of the session of the tab (the shell copies it
  // into its system menu while the tab is the active one), or 0.
  virtual HMENU sessionMenu() const { return 0; }

  // Child window of the shell that shows the tab.
  virtual HWND viewHwnd() const = 0;
  // Window that gets the keyboard focus when the shell is activated.
  virtual HWND focusHwnd() const = 0;

  // WM_COMMAND and WM_SYSCOMMAND of the shell (menus, accelerators, the full
  // screen toolbar). Returns true when the tab handled the command.
  virtual bool onCommand(WPARAM wParam, LPARAM lParam) = 0;
  // WM_ACTIVATE of the shell.
  virtual void onActivate(WPARAM wParam) = 0;
  // The user is resizing the shell (WM_SIZING).
  virtual void onFrameSizing() = 0;
  // WM_DISPLAYCHANGE of the shell.
  virtual void onDisplayChange() = 0;
  // WM_DPICHANGED of the shell, before the shell takes the rectangle
  // suggested by the system (frameMoved) or keeps its full screen size.
  virtual void onDpiChanged(UINT dpi, bool frameMoved) = 0;

  // The user closes the shell (WM_CLOSE). Returns true when the shell can
  // be destroyed at once; a session closes itself instead and ends the
  // shell through SessionHost::onSessionEnded().
  virtual bool requestClose() { return true; }
  // State of the application shown by the tab changed (listening mode).
  virtual void onAppStateChanged() {}

  // The Settings page covers the tab, whose view the shell hides (a session
  // keeps running), or the tab is shown again. The shell moves the focus
  // away before covering and sends WM_ACTIVATE again after uncovering.
  virtual void onCovered() {}
  virtual void onUncovered() {}

private:
  unsigned m_id;
  bool m_closing;
  StringStorage m_windowTitle;
  std::wstring m_tabTitle;
};

#endif
