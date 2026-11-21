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

#ifndef _AUTHENTICATION_DIALOG_H_
#define _AUTHENTICATION_DIALOG_H_

#include "gui/BaseDialog.h"
#include "gui/TextBox.h"
#include "resource.h"
#include "DialogTheme.h"

class AuthenticationDialog : public BaseDialog
{
public:
  AuthenticationDialog();

  // this function returns the password entered by user
  const StringStorage *getPassword();

  // this function returns sets the name of host in dialog
  void setHostName(const StringStorage *hostname);

protected:
  BOOL onCommand(UINT controlID, UINT notificationID);
  BOOL onInitDialog();

  TextBox m_password;
  StringStorage m_strPassword;
  StringStorage m_strHost;

private:
  // Show password: shows or masks the field again with its own mask char.
  void showPassword(bool show);

  DialogTheme m_theme;
  WPARAM m_passwordChar;
};

#endif
