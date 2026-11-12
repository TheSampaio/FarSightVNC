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

#ifndef _SETTINGS_SECTIONS_H_
#define _SETTINGS_SECTIONS_H_

#include "SettingsActions.h"
#include "SettingsSection.h"

#include <memory>

class ConnectionConfig;

// The sections of the Settings page.
namespace SettingsSections
{
  // Name of a section (navigation and heading) and the codepoint of its
  // placeholder glyph in the system icon font (Segoe Fluent Icons / Segoe
  // MDL2 Assets) until the Lucide icons.
  const wchar_t *title(SettingsSectionId section);
  wchar_t glyph(SettingsSectionId section);

  // Connection and Input: the options of new connections (defaults, saved
  // in History\.default at every change), with the logic of the Options
  // dialog (ConnectionOptionsForm).
  std::unique_ptr<SettingsSection> connection(ConnectionConfig *defaults);
  std::unique_ptr<SettingsSection> input(ConnectionConfig *defaults);
  // A section that comes in the next step: its title and a short text,
  // with the legacy dialog that still holds its settings, if any.
  std::unique_ptr<SettingsSection> placeholder(SettingsSectionId section,
                                               SettingsActions *actions);
}

#endif
