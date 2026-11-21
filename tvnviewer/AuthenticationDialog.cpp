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
// Modified for FarSightVNC on 2026-11-20: new UI (theme, heading, host line, Show password).
//

#include "AuthenticationDialog.h"

AuthenticationDialog::AuthenticationDialog()
: BaseDialog(IDD_DAUTH),
  m_passwordChar(0)
{
}


BOOL AuthenticationDialog::onInitDialog() 
{
  HWND window = m_ctrlThis.getWindow();
  StringStorage prompt;
  prompt.format(_T("Enter the password for %s."), m_strHost.getString());
  SetDlgItemText(window, IDC_AUTH_PROMPT, prompt.getString());
  setControlById(m_password, IDC_EPASSW);
  m_passwordChar = SendMessage(m_password.getWindow(), EM_GETPASSWORDCHAR, 0, 0);
  if (m_passwordChar == 0) {
    m_passwordChar = _T('*');
  }

  m_theme.setHeading(IDC_AUTH_HEADING);
  m_theme.setMuted(IDC_AUTH_PROMPT);
  m_theme.attach(window);

  // The dialog comes from the connection thread and has no owner: it must
  // come to the front by itself, with the caret in the password field.
  setForeground();
  m_password.setFocus();
  return FALSE;
}

void AuthenticationDialog::showPassword(bool show)
{
  HWND field = m_password.getWindow();
  SendMessage(field, EM_SETPASSWORDCHAR, show ? 0 : m_passwordChar, 0);
  InvalidateRect(field, 0, TRUE);
}

void AuthenticationDialog::setHostName(const StringStorage *hostname)
{
  m_strHost = *hostname;
}

BOOL AuthenticationDialog::onCommand(UINT controlID, UINT notificationID)
{
  if (controlID == IDOK) {
    m_password.getText(&m_strPassword);
    kill(1);
    return TRUE;
  }
  if (controlID == IDCANCEL) {
    kill(0);
    return TRUE;
  }
  if (controlID == IDC_AUTH_SHOW && notificationID == BN_CLICKED) {
    showPassword(IsDlgButtonChecked(m_ctrlThis.getWindow(), IDC_AUTH_SHOW) == BST_CHECKED);
    return TRUE;
  }
  return FALSE;
}

const StringStorage *AuthenticationDialog::getPassword()
{
  return &m_strPassword;
}
