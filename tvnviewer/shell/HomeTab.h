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

#ifndef _HOME_TAB_H_
#define _HOME_TAB_H_

#include "ShellTab.h"
#include "HomePage.h"
#include "../resource.h"

// Start tab of a shell: owns the Home page. The commands of the title bar
// and of the menus that make sense without a session (settings, new
// connection) go to the application.
class HomeTab : public ShellTab
{
public:
  HomeTab(HWND shell, HomeActions *actions)
  : m_actions(actions),
    m_page(shell, actions)
  {
  }

  virtual HWND viewHwnd() const { return m_page.getHWnd(); }
  virtual HWND focusHwnd() const { return m_page.focusHwnd(); }

  virtual bool onCommand(WPARAM wParam, LPARAM lParam)
  {
    switch (LOWORD(wParam)) {
    case IDS_TB_CONFIGURATION:
      m_actions->openConfiguration();
      return true;
    case IDS_TB_NEWCONNECTION:
      m_actions->openHome();
      return true;
    }
    return false;
  }

  virtual void onActivate(WPARAM wParam)
  {
    // Another window may have changed the favorites or the history.
    if (LOWORD(wParam) != WA_INACTIVE) {
      m_page.refresh();
    }
  }
  virtual void onFrameSizing() {}
  virtual void onDisplayChange() {}
  virtual void onDpiChanged(UINT dpi, bool) { m_page.setDpi(dpi); }
  virtual void onAppStateChanged() { m_page.refresh(); }

private:
  HomeActions *m_actions;
  HomePage m_page;
};

#endif
