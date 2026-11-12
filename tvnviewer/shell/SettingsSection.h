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

#ifndef _SETTINGS_SECTION_H_
#define _SETTINGS_SECTION_H_

#include "util/winhdr.h"

// Sections of the Settings page, in the order of its navigation.
enum SettingsSectionId
{
  SETTINGS_APPEARANCE,
  SETTINGS_CONNECTION,
  SETTINGS_INPUT,
  SETTINGS_DISPLAY,
  SETTINGS_GENERAL,
  SETTINGS_UPDATES,
  SETTINGS_ABOUT,
  SETTINGS_SECTION_COUNT
};

class SettingsForm;

// Content of one section: it adds its rows and native controls to the form,
// fills them from the stored settings and applies every change at once (the
// numbers of edit fields when they lose the focus).
class SettingsSection
{
public:
  virtual ~SettingsSection() {}

  // Adds the rows and loads the values. The form is empty.
  virtual void build(SettingsForm *form) = 0;
  // WM_COMMAND of a control of the form.
  virtual void onCommand(UINT controlId, UINT notification) {}
  // WM_HSCROLL of a track bar of the form.
  virtual void onHScroll(HWND control) {}
  // Applies what is still pending (an edit field being typed in); called
  // before the controls are destroyed.
  virtual void commit() {}
};

#endif
