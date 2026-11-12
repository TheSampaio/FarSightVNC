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

#ifndef _SETTINGS_ACTIONS_H_
#define _SETTINGS_ACTIONS_H_

#include "util/winhdr.h"

// What the Settings page asks of the application (TvnViewer).
class SettingsActions
{
public:
  virtual ~SettingsActions() {}

  // The message loop gives the keys of a window to the dialog manager
  // (IsDialogMessage: Tab, arrows, Enter, Esc), like a modeless dialog.
  virtual void addDialogWindow(HWND window) = 0;
  virtual void removeDialogWindow(HWND window) = 0;

  // Opens the configuration dialog of the viewer (posted). The General
  // section replaces it in the next step.
  virtual void openConfigurationDialog() = 0;
  // Runs the update check now, like "Check for updates..." (posted).
  virtual void checkForUpdates() = 0;
};

#endif
