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
#include "../UpdateController.h"

// What the Settings page asks of the application (TvnViewer).
class SettingsActions
{
public:
  virtual ~SettingsActions() {}

  // The message loop gives the keys of a window to the dialog manager
  // (IsDialogMessage: Tab, arrows, Enter, Esc), like a modeless dialog.
  virtual void addDialogWindow(HWND window) = 0;
  virtual void removeDialogWindow(HWND window) = 0;

  // ViewerConfig changed and was saved: the listening mode follows a new
  // port (posted), the Home pages refresh.
  virtual void onViewerConfigChanged() = 0;

  // Updates (UpdateController).
  virtual UpdateController::Preferences updatePreferences() = 0;
  virtual void setUpdateAutoCheck(bool enabled) = 0;
  virtual void setUpdatePrereleases(bool enabled) = 0;
  // Runs the update check now, like "Check for updates..." (posted).
  virtual void checkForUpdates() = 0;
};

#endif
