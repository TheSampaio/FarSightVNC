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

#include "SettingsSections.h"

#include "SettingsForm.h"
#include "../ConnectionOptionsForm.h"
#include "../DefaultConnectionConfig.h"
#include "../resource.h"

namespace {

// Texts of the sections (the viewer UI is in English).
const wchar_t kConnectionSubtitle[] =
  L"Options of new connections. A computer you connected to before keeps the "
  L"options it was last used with.";
const wchar_t kInputSubtitle[] =
  L"Keyboard, mouse and clipboard options of new connections.";
const wchar_t kNextBuild[] = L"This section arrives in the next build.";
const wchar_t kGeneralLegacy[] =
  L"Until then, the toolbar, history, logging and listening port settings are "
  L"in the configuration dialog.";
const wchar_t kOpenConfiguration[] = L"Open configuration\x2026";
const wchar_t kCheckNow[] = L"Check now";

struct SectionInfo
{
  const wchar_t *title;
  wchar_t glyph;
};

// Segoe Fluent Icons / Segoe MDL2 Assets placeholders (Lucide: palette,
// plug, keyboard, monitor, settings, refresh-cw, info).
const SectionInfo kSections[SETTINGS_SECTION_COUNT] = {
  { L"Appearance", 0xE790 },   // Color
  { L"Connection", 0xE703 },   // Connect
  { L"Input", 0xE765 },        // KeyboardClassic
  { L"Display", 0xE7F4 },      // TVMonitor
  { L"General", 0xE713 },      // Setting
  { L"Updates", 0xE895 },      // Sync
  { L"About", 0xE946 },        // Info
};

// Commands of the placeholder sections (no resource control uses them).
const UINT ID_OPEN_CONFIGURATION = 0x5F01;
const UINT ID_CHECK_NOW = 0x5F02;

// The connection options of one section, applied to the defaults of new
// connections and saved at every change.
class ConnectionOptionsSection : public SettingsSection
{
public:
  ConnectionOptionsSection(SettingsSectionId id, ConnectionConfig *defaults)
  : m_id(id),
    m_defaults(defaults)
  {
  }

  virtual void build(SettingsForm *form)
  {
    if (m_id == SETTINGS_CONNECTION) {
      buildConnection(form);
    } else {
      buildInput(form);
    }
    m_options.bind([form](UINT controlId) { return form->control(controlId); });
    m_options.load(m_defaults, false);
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    if (m_options.onCommand(controlId, notification)) {
      save();
    }
  }

  virtual void onHScroll(HWND control)
  {
    if (m_options.onHScroll(control)) {
      save();
    }
  }

private:
  void buildConnection(SettingsForm *form)
  {
    form->setHeading(SettingsSections::title(m_id), kConnectionSubtitle);
    form->header(L"Encoding");
    form->comboBox(IDC_CUSEENC, L"Preferred encoding", 160.0f);
    form->checkBox(IDC_CEIGHTBIT, L"256 colors (less traffic)");
    form->checkBox(IDC_CCOMPRLVL, L"Custom compression level");
    form->trackBar(IDC_SCOMP, IDC_SFAST, L"Fast", IDC_SBEST, L"Best", IDC_SQUALITY, 1);
    form->checkBox(IDC_CJPEG, L"Allow JPEG, with image quality");
    form->trackBar(IDC_SJPEG, IDC_SPOOR, L"Poor", IDC_SBEST2, L"Best", IDC_SQUALITY2, 1);
    form->checkBox(IDC_CCOPYRECT, L"Allow CopyRect encoding");
    form->header(L"Session");
    form->checkBox(IDC_CSHAREDSES, L"Request a shared session (other viewers stay connected)");
    form->checkBox(IDC_CFULLSCR, L"Start in full screen");
    form->checkBox(IDC_CDEICONFY, L"Restore the window on a remote bell");
    form->header(L"Remote cursor");
    form->radio(IDC_RTRACK, L"Track the remote cursor locally", true);
    form->radio(IDC_RCURSOR, L"Let the remote computer draw the cursor", false);
    form->radio(IDC_RNCURSOR, L"Don't show the remote cursor", false);
    form->header(L"Local cursor shape");
    form->radio(IDC_RDOT, L"Normal dot", true);
    form->radio(IDC_RSMALLDOT, L"Small dot", false);
    form->radio(IDC_RARROW, L"Arrow", false);
    form->radio(IDC_RNLOCAL, L"No local cursor", false);
  }

  void buildInput(SettingsForm *form)
  {
    form->setHeading(SettingsSections::title(m_id), kInputSubtitle);
    form->header(L"Restrictions");
    form->checkBox(IDC_CVIEWONLY, L"View only (keyboard and mouse are not sent)");
    form->checkBox(IDC_CDISCLIP, L"Disable clipboard transfer");
    form->header(L"Mouse");
    form->checkBox(IDC_CSWAPMOUSE, L"Swap the right and middle buttons");
  }

  void save()
  {
    m_options.save(m_defaults);
    DefaultConnectionConfig::save(m_defaults);
  }

  SettingsSectionId m_id;
  ConnectionConfig *m_defaults;
  ConnectionOptionsForm m_options;
};

class PlaceholderSection : public SettingsSection
{
public:
  PlaceholderSection(SettingsSectionId id, SettingsActions *actions)
  : m_id(id),
    m_actions(actions)
  {
  }

  virtual void build(SettingsForm *form)
  {
    form->setHeading(SettingsSections::title(m_id), kNextBuild);
    if (m_id == SETTINGS_GENERAL) {
      form->note(kGeneralLegacy);
      form->button(ID_OPEN_CONFIGURATION, kOpenConfiguration);
    } else if (m_id == SETTINGS_UPDATES) {
      form->button(ID_CHECK_NOW, kCheckNow);
    }
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    if (notification != BN_CLICKED) {
      return;
    }
    if (controlId == ID_OPEN_CONFIGURATION) {
      m_actions->openConfigurationDialog();
    } else if (controlId == ID_CHECK_NOW) {
      m_actions->checkForUpdates();
    }
  }

private:
  SettingsSectionId m_id;
  SettingsActions *m_actions;
};

}

const wchar_t *SettingsSections::title(SettingsSectionId section)
{
  return section < SETTINGS_SECTION_COUNT ? kSections[section].title : L"";
}

wchar_t SettingsSections::glyph(SettingsSectionId section)
{
  return section < SETTINGS_SECTION_COUNT ? kSections[section].glyph : 0;
}

std::unique_ptr<SettingsSection> SettingsSections::connection(ConnectionConfig *defaults)
{
  return std::unique_ptr<SettingsSection>(new ConnectionOptionsSection(SETTINGS_CONNECTION, defaults));
}

std::unique_ptr<SettingsSection> SettingsSections::input(ConnectionConfig *defaults)
{
  return std::unique_ptr<SettingsSection>(new ConnectionOptionsSection(SETTINGS_INPUT, defaults));
}

std::unique_ptr<SettingsSection> SettingsSections::placeholder(SettingsSectionId section,
                                                               SettingsActions *actions)
{
  return std::unique_ptr<SettingsSection>(new PlaceholderSection(section, actions));
}
