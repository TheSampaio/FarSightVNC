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

#ifndef _CONNECTION_OPTIONS_FORM_H_
#define _CONNECTION_OPTIONS_FORM_H_

#include "client-config-lib/ConnectionConfig.h"
#include "gui/CheckBox.h"
#include "gui/ComboBox.h"
#include "gui/Control.h"
#include "gui/TrackBar.h"

#include <functional>

// The connection options controls (encoding, compression, JPEG, colors,
// restrictions, cursor, full screen...) and their logic: filling them from a
// ConnectionConfig, the dependencies between them and writing them back. The
// controls are found by their resource IDs (IDC_CUSEENC, IDC_CVIEWONLY...),
// wherever they live: the tab pages of the Options dialog or a section of the
// Settings page. A control that is missing is simply skipped.
class ConnectionOptionsForm
{
public:
  // Returns the window of a control ID, or 0 when the form has no such
  // control.
  typedef std::function<HWND(UINT)> ControlFinder;

  ConnectionOptionsForm();

  // Finds the controls and fills the fixed lists and ranges. Called once,
  // after the controls exist.
  void bind(const ControlFinder &finder);

  // Shows the options of config. connected: the session already runs (the
  // shared flag cannot change any more).
  void load(const ConnectionConfig *config, bool connected);
  // Writes the options shown into config.
  void save(ConnectionConfig *config);

  // WM_COMMAND of a control. Returns true when the notification changed a
  // value of the form.
  bool onCommand(UINT controlID, UINT notificationID);
  // WM_HSCROLL of a track bar. Returns true when it is one of the form.
  bool onHScroll(HWND control);

private:
  HWND findControl(UINT controlID) const;
  void enableControl(UINT controlID, bool enable);
  void onViewOnlyClick();
  void on8BitColorClick();
  void enableJpegCompression(bool enable);
  void onAllowCustomCompressionClick();
  void enableCustomCompression(bool enable);
  void onAllowJpegCompressionClick();
  void onPreferredEncodingSelectionChange();
  void onCustomCompressionLevelScroll();
  void onJpegCompressionLevelScroll();

  ControlFinder m_finder;

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
};

#endif
