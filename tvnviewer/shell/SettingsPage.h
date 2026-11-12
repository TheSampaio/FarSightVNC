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

#ifndef _SETTINGS_PAGE_H_
#define _SETTINGS_PAGE_H_

#include "SettingsActions.h"
#include "SettingsForm.h"
#include "SettingsNav.h"
#include "SettingsSection.h"
#include "client-config-lib/ConnectionConfig.h"
#include "gui/d2d/ChromeRenderer.h"

#include <memory>

// Settings of the viewer inside the shell window, in place of the tab it
// covers: a "Back" button and the section navigation in a left column drawn
// with ChromeRenderer, and the controls of the selected section on the right
// (SettingsForm). Every change applies at once.
//
// Keyboard: the page is handled by the dialog manager (SettingsActions::
// addDialogWindow): Tab and Shift+Tab go from Back to the navigation and
// through the controls of the section, the arrows move in the navigation and
// in radio groups, Esc goes back. The shell closes the page when it receives
// closeMessage().
class SettingsPage : public SettingsFormListener
{
public:
  SettingsPage(HWND shell, SettingsActions *actions, SettingsSectionId section);
  ~SettingsPage();

  HWND getHWnd() const { return m_hwnd; }
  // The navigation, which takes the focus when the page is shown.
  HWND focusHwnd() const;

  void setDpi(UINT dpi);
  void showSection(SettingsSectionId section);
  SettingsSectionId section() const { return m_sectionId; }
  // Applies what is pending (an edit field being typed in).
  void commit();

  // Message posted to the shell when the user leaves the page (Back, Esc).
  static UINT closeMessage();

  // SettingsFormListener.
  virtual void onFormCommand(UINT controlId, UINT notification, HWND control);
  virtual void onFormHScroll(HWND control);

private:
  SettingsPage(const SettingsPage &);
  SettingsPage &operator=(const SettingsPage &);

  static bool registerClass();
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void moveFocus(WPARAM wParam, LPARAM lParam);
  std::unique_ptr<SettingsSection> createSection(SettingsSectionId section);
  void layout();
  void paint();
  void back();

  HWND m_hwnd;
  // The page as given to addDialogWindow(), removed even when the shell
  // destroyed the window first.
  HWND m_dialogHwnd;
  HWND m_shell;
  SettingsActions *m_actions;
  ChromeRenderer m_chrome;
  std::unique_ptr<SettingsNav> m_back;
  std::unique_ptr<SettingsNav> m_nav;
  std::unique_ptr<SettingsForm> m_form;
  std::unique_ptr<SettingsSection> m_section;
  SettingsSectionId m_sectionId;
  // Options of new connections, edited by the Connection and Input sections.
  ConnectionConfig m_defaultConfig;
};

#endif
