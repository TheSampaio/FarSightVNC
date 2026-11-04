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
//

#ifndef _OPTIONS_DIALOG_H_
#define _OPTIONS_DIALOG_H_

#include "client-config-lib/ViewerConfig.h"
#include "client-config-lib/ConnectionConfig.h"
#include "client-config-lib/ConnectionConfigSM.h"
#include "client-config-lib/ViewerSettingsManager.h"
#include "util/StringParser.h"
#include "gui/BaseDialog.h"
#include "gui/TextBox.h"
#include "gui/CheckBox.h"
#include "gui/ComboBox.h"
#include "gui/TrackBar.h"
#include "gui/TabControl.h"
#include "resource.h"

class OptionsDialog;

// One tab page of the Options dialog. It only hosts the controls of its
// dialog template and forwards their notifications to the owner dialog, which
// keeps all the option logic.
class OptionsPage : public BaseDialog
{
public:
  OptionsPage(DWORD resourceId, OptionsDialog *owner);

protected:
  BOOL onInitDialog();
  BOOL onCommand(UINT controlID, UINT notificationID);
  void onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
  OptionsDialog *m_owner;
};

class OptionsDialog : public BaseDialog
{
  friend class OptionsPage;

public:
  OptionsDialog();

  void setConnectionConfig(ConnectionConfig *conConfig);
  void setConnected();

protected:
  ComboBox m_useEnc;
  CheckBox m_eightBit;
  CheckBox m_compLvl;
  Trackbar m_tcompLvl;
  Control m_quality;
  CheckBox m_jpeg;
  Trackbar m_tjpeg;
  Control m_quality2;
  CheckBox m_copyrect;
  CheckBox m_viewonly;
  CheckBox m_disclip;
  CheckBox m_sharedses;
  ComboBox m_scale;
  CheckBox m_fullscr;
  CheckBox m_deiconfy;
  CheckBox m_swapmouse;
  CheckBox m_track;       // Track remote control locally
  CheckBox m_cursor;      // Let remote server deal with mouse cursor
  CheckBox m_ncursor;     // Don't show remote cursor
  CheckBox m_dot;
  CheckBox m_smalldot;
  CheckBox m_arrow;
  CheckBox m_nlocal;

  void onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam);
  BOOL onCommand(UINT controlID, UINT notificationID);
  BOOL onNotify(UINT controlID, LPARAM data);
  BOOL onInitDialog();
  BOOL onDestroy();

  ConnectionConfig *m_conConfig;
  bool m_connected;

private:
  BOOL onControlCommand(UINT controlID, UINT notificationID);
  void createPage(OptionsPage *page, const TCHAR *caption);
  HWND findControl(UINT controlID);
  void setPageControl(Control &control, DWORD controlID);
  bool onTabSwitchKey(const MSG *msg);
  static LRESULT CALLBACK msgFilterProc(int code, WPARAM wParam, LPARAM lParam);
  void updateControlValues();
  void onViewOnlyClick();
  void on8BitColorClick();
  void enableJpegCompression(bool enable);
  void onAllowCustomCompressionClick();
  void enableCustomCompression(bool enable);
  void onAllowJpegCompressionClick();
  void onPreferredEncodingSelectionChange();
  void onCustomCompressionLevelScroll();
  void onJpegCompressionLevelScroll();
  void onScaleKillFocus();
  void showScaleField();
  bool isInputValid();
  bool onOkPressed();
  void apply();

  TabControl m_tabControl;
  OptionsPage m_displayPage;
  OptionsPage m_inputPage;
  OptionsPage m_networkPage;
  HHOOK m_msgHook;

  // The hook procedure has no user data, so the open Options dialogs are kept
  // in a list. Several viewer windows can open their own modal Options dialog,
  // each one nested in the modal loop of the previous one.
  OptionsDialog *m_previousDialog;
  static OptionsDialog *s_activeDialog;
};

#endif
