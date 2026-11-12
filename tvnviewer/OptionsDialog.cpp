// Copyright (C) 2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
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
//-------------------------------------------------------------------------
//
// Modified for FarSightVNC on 2026-11-03: Options dialog split into Display,
// Input and Network tab pages, with Ctrl+Tab switching between them.
// Modified for FarSightVNC on 2026-11-11: the scale control shows "Fit to window", disabled (the remote screen always fits).
// Modified for FarSightVNC on 2026-11-11: the option logic moved to ConnectionOptionsForm, shared with the Settings page.
//

#include "OptionsDialog.h"

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

OptionsPage::OptionsPage(DWORD resourceId, OptionsDialog *owner)
: BaseDialog(resourceId),
  m_owner(owner)
{
}

BOOL OptionsPage::onInitDialog()
{
  // Paint the page background like the tab body.
  EnableThemeDialogTexture(m_ctrlThis.getWindow(), ETDT_ENABLETAB);
  return FALSE;
}

BOOL OptionsPage::onCommand(UINT controlID, UINT notificationID)
{
  return m_owner->onControlCommand(controlID, notificationID);
}

void OptionsPage::onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  m_owner->onMessageReceived(uMsg, wParam, lParam);
}

OptionsDialog *OptionsDialog::s_activeDialog = NULL;

OptionsDialog::OptionsDialog()
: BaseDialog(IDD_OPTIONS),
  m_connected(false),
  m_displayPage(IDD_OPTIONS_DISPLAY, this),
  m_inputPage(IDD_OPTIONS_INPUT, this),
  m_networkPage(IDD_OPTIONS_NETWORK, this),
  m_msgHook(NULL),
  m_previousDialog(NULL)
{
}

void OptionsDialog::setConnectionConfig(ConnectionConfig *conConfig)
{
  m_conConfig = conConfig;
}

void OptionsDialog::setConnected()
{
  m_connected = true;
}

BOOL OptionsDialog::onCommand(UINT controlID, UINT notificationID)
{
  if (controlID == IDOK) {
    if (onOkPressed()) {
      kill(1);
    }
    return TRUE;
  }
  if (controlID == IDCANCEL) {
    kill(0);
    return TRUE;
  }
  return FALSE;
}

BOOL OptionsDialog::onControlCommand(UINT controlID, UINT notificationID)
{
  return m_form.onCommand(controlID, notificationID) ? TRUE : FALSE;
}

BOOL OptionsDialog::onNotify(UINT controlID, LPARAM data)
{
  if (controlID == IDC_OPTIONS_TAB) {
    Tab *tab = m_tabControl.getTab(m_tabControl.getSelectedTabIndex());
    switch (reinterpret_cast<LPNMHDR>(data)->code) {
    case TCN_SELCHANGING:
      tab->setVisible(false);
      break;
    case TCN_SELCHANGE:
      tab->setVisible(true);
      break;
    }
  }
  return FALSE;
}

void OptionsDialog::createPage(OptionsPage *page, const TCHAR *caption)
{
  page->setParent(&m_ctrlThis);
  page->create();

  // Fit the page into the display area of the tab control.
  RECT rect;
  m_tabControl.adjustRect(&rect);
  MapWindowPoints(m_tabControl.getWindow(), m_ctrlThis.getWindow(),
                  reinterpret_cast<POINT *>(&rect), 2);
  MoveWindow(page->getControl()->getWindow(), rect.left, rect.top,
             rect.right - rect.left, rect.bottom - rect.top, TRUE);

  // Put the page right after the tab control in the Z order, so that Tab
  // goes from the tab control to the page controls and then to OK and Cancel.
  SetWindowPos(page->getControl()->getWindow(), m_tabControl.getWindow(),
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

  m_tabControl.addTab(page, caption);
}

HWND OptionsDialog::findControl(UINT controlID)
{
  OptionsPage *pages[] = {&m_displayPage, &m_inputPage, &m_networkPage};
  for (int i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
    HWND control = GetDlgItem(pages[i]->getControl()->getWindow(), controlID);
    if (control != NULL) {
      return control;
    }
  }
  return NULL;
}

// Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PgDn and Ctrl+PgUp switch the tab. A dialog
// does this only for property sheets, so it is done here.
bool OptionsDialog::onTabSwitchKey(const MSG *msg)
{
  HWND dialog = m_ctrlThis.getWindow();
  if (msg->hwnd != dialog && !IsChild(dialog, msg->hwnd)) {
    return false;
  }
  if (GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_MENU) < 0) {
    return false;
  }
  int step;
  if (msg->wParam == VK_TAB) {
    step = (GetKeyState(VK_SHIFT) < 0) ? -1 : 1;
  } else if (msg->wParam == VK_NEXT) {
    step = 1;
  } else if (msg->wParam == VK_PRIOR) {
    step = -1;
  } else {
    return false;
  }

  int count = static_cast<int>(m_tabControl.getTabs().size());
  int index = (m_tabControl.getSelectedTabIndex() + step + count) % count;
  bool tabHadFocus = (GetFocus() == m_tabControl.getWindow());
  m_tabControl.showTab(index);
  if (!tabHadFocus) {
    HWND page = m_tabControl.getTab(index)->getDialog()->getControl()->getWindow();
    HWND first = GetNextDlgTabItem(page, NULL, FALSE);
    if (first != NULL) {
      SetFocus(first);
    }
  }
  return true;
}

LRESULT CALLBACK OptionsDialog::msgFilterProc(int code, WPARAM wParam, LPARAM lParam)
{
  if (code == MSGF_DIALOGBOX) {
    const MSG *msg = reinterpret_cast<const MSG *>(lParam);
    if (msg->message == WM_KEYDOWN) {
      for (OptionsDialog *dialog = s_activeDialog; dialog != NULL;
           dialog = dialog->m_previousDialog) {
        if (dialog->onTabSwitchKey(msg)) {
          return 1;
        }
      }
    }
  }
  return CallNextHookEx(NULL, code, wParam, lParam);
}

BOOL OptionsDialog::onDestroy()
{
  if (m_msgHook != NULL) {
    UnhookWindowsHookEx(m_msgHook);
    m_msgHook = NULL;
    for (OptionsDialog **link = &s_activeDialog; *link != NULL;
         link = &(*link)->m_previousDialog) {
      if (*link == this) {
        *link = m_previousDialog;
        break;
      }
    }
    m_previousDialog = NULL;
  }
  m_tabControl.deleteAllTabs();
  return TRUE;
}

BOOL OptionsDialog::onInitDialog()
{
  m_tabControl.setWindow(GetDlgItem(m_ctrlThis.getWindow(), IDC_OPTIONS_TAB));
  createPage(&m_displayPage, StringTable::getString(IDS_OPTIONS_TAB_DISPLAY));
  createPage(&m_inputPage, StringTable::getString(IDS_OPTIONS_TAB_INPUT));
  createPage(&m_networkPage, StringTable::getString(IDS_OPTIONS_TAB_NETWORK));
  m_tabControl.showTab(0);

  if (m_msgHook == NULL) {
    m_msgHook = SetWindowsHookEx(WH_MSGFILTER, msgFilterProc, NULL,
                                 GetCurrentThreadId());
    if (m_msgHook != NULL) {
      m_previousDialog = s_activeDialog;
      s_activeDialog = this;
    }
  }

  m_form.bind([this](UINT controlID) { return findControl(controlID); });
  m_form.load(m_conConfig, m_connected);
  m_tabControl.setFocus();
  return FALSE;
}

void OptionsDialog::onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_HSCROLL) {
    m_form.onHScroll(reinterpret_cast<HWND>(lParam));
  }
}

bool OptionsDialog::onOkPressed()
{
  m_form.save(m_conConfig);
  return true;
}
