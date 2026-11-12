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

#ifndef _SETTINGS_TAB_H_
#define _SETTINGS_TAB_H_

#include "ShellTab.h"
#include "SettingsPage.h"

// The Settings page shown by a shell over its active tab (Home or a session),
// which it covers until the user goes back. It owns the page.
class SettingsTab : public ShellTab
{
public:
  SettingsTab(HWND shell, SettingsActions *actions, SettingsSectionId section)
  : m_page(shell, actions, section)
  {
  }

  void showSection(SettingsSectionId section) { m_page.showSection(section); }
  SettingsSectionId section() const { return m_page.section(); }

  virtual HWND viewHwnd() const { return m_page.getHWnd(); }
  virtual HWND focusHwnd() const { return m_page.focusHwnd(); }
  // The shell handles the settings command; the page has no other one.
  virtual bool onCommand(WPARAM, LPARAM) { return false; }
  virtual void onActivate(WPARAM) {}
  virtual void onFrameSizing() {}
  virtual void onDisplayChange() {}
  virtual void onDpiChanged(UINT dpi, bool) { m_page.setDpi(dpi); }

private:
  SettingsPage m_page;
};

#endif
