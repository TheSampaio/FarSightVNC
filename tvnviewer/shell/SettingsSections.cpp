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
// Modified for FarSightVNC on 2026-11-12: Third-party notices (Lucide) in About.
// Modified for FarSightVNC on 2026-11-12: the Appearance theme is applied, not only stored.
//

#include "SettingsSections.h"

#include "SettingsForm.h"
#include "gui/d2d/Theme.h"
#include "client-config-lib/ViewerConfig.h"
#include "client-config-lib/ViewerSettingsManager.h"
#include "file-lib/File.h"
#include "util/ResourceLoader.h"
#include "win-system/Environment.h"
#include "win-system/Process.h"
#include "win-system/Shell.h"
#include "../ConnectionOptionsForm.h"
#include "../DefaultConnectionConfig.h"
#include "../NamingDefs.h"
#include "../RenderSettings.h"
#include "../resource.h"

#include <ctime>
#include <string>
#include <vector>

namespace {

// Texts of the sections (the viewer UI is in English).
const wchar_t kAppearanceSubtitle[] =
  L"Choose how FarSightVNC looks. Auto follows the Windows app theme.";
const wchar_t kConnectionSubtitle[] =
  L"Options of new connections. A computer you connected to before keeps the "
  L"options it was last used with.";
const wchar_t kInputSubtitle[] =
  L"Keyboard, mouse and clipboard options of new connections.";
const wchar_t kDisplaySubtitle[] = L"How the remote screen is drawn.";
const wchar_t kGeneralSubtitle[] = L"Toolbar, history, listening mode and logging.";
const wchar_t kUpdatesSubtitle[] = L"FarSightVNC can check GitHub for new versions.";
const wchar_t kAboutSubtitle[] = L"FarSightVNC for Windows, a remote desktop viewer.";
const wchar_t kLicense[] =
  L"This program is free software; you can redistribute it and/or modify it under the "
  L"terms of the GNU General Public License, version 2 or (at your option) any later "
  L"version. It comes WITH ABSOLUTELY NO WARRANTY; see the license for details.";
// Notice of Lucide (vendor/lucide/LICENSE of lucide-static 0.468.0): ISC, with
// portions from Feather (MIT). The copyright line is kept as in that file and
// must stay in sync with docs/shipping/LEIAME.txt and README.txt.
const wchar_t kLucideSource[] =
  L"Icons: Lucide (https://lucide.dev), lucide-static 0.468.0, ISC License.";
const wchar_t kLucideCopyright[] =
  L"Copyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as part of "
  L"Feather (MIT). All other copyright (c) for Lucide are held by Lucide Contributors 2022.";
const wchar_t kLucidePermission[] =
  L"Permission to use, copy, modify, and/or distribute this software for any purpose with "
  L"or without fee is hereby granted, provided that the above copyright notice and this "
  L"permission notice appear in all copies.";
const wchar_t kLucideDisclaimer[] =
  L"THE SOFTWARE IS PROVIDED \"AS IS\" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD "
  L"TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO "
  L"EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL "
  L"DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER "
  L"IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN "
  L"CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.";

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

// Controls of the sections that have no resource ID (0x5F00 and above are
// not used by the resources).
enum ControlId
{
  CTL_THEME_AUTO = 0x5F01,
  CTL_THEME_LIGHT,
  CTL_THEME_DARK,
  CTL_RENDERER_GDI,
  CTL_RENDERER_AUTO,
  CTL_RENDERER_GPU,
  CTL_SHOW_TOOLBAR,
  CTL_WARN_FULLSCREEN,
  CTL_HISTORY_LIMIT,
  CTL_CLEAR_HISTORY,
  CTL_LISTEN_PORT,
  CTL_LOG_LEVEL,
  CTL_LOG_FILE,
  CTL_LOCATE_LOG,
  CTL_AUTO_CHECK,
  CTL_PRERELEASES,
  CTL_CHECK_NOW,
  CTL_WEB_SITE,
  CTL_LICENSE
};

std::wstring toWide(const TCHAR *text)
{
  if (text == 0) {
    return std::wstring();
  }
#ifdef UNICODE
  return std::wstring(text);
#else
  int length = MultiByteToWideChar(CP_ACP, 0, text, -1, 0, 0);
  if (length <= 1) {
    return std::wstring();
  }
  std::wstring result(length - 1, L'\0');
  MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], length);
  return result;
#endif
}

void check(HWND control, bool checked)
{
  SendMessage(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool isChecked(HWND control)
{
  return SendMessage(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void setNumber(HWND field, int value)
{
  SetWindowTextW(field, std::to_wstring(value).c_str());
}

// The number of a field, or false when it is empty or not a number.
bool getNumber(HWND field, int *value)
{
  wchar_t text[16] = L"";
  GetWindowTextW(field, text, 16);
  if (text[0] == 0) {
    return false;
  }
  long result = 0;
  for (const wchar_t *c = text; *c != 0; c++) {
    if (*c < L'0' || *c > L'9' || result > 1000000) {
      return false;
    }
    result = result * 10 + (*c - L'0');
  }
  *value = static_cast<int>(result);
  return true;
}

void saveViewerConfig()
{
  ViewerConfig::getInstance()->saveToStorage(ViewerSettingsManager::getInstance());
}

void openUrl(UINT stringId)
{
  try {
    Shell::open(StringTable::getString(stringId), 0, 0);
  } catch (...) {
    MessageBeep(MB_ICONWARNING);
  }
}

// Product version of the executable ("1.0.0a"), from its resource.
std::wstring productVersion()
{
  StringStorage path;
  Environment::getCurrentModulePath(&path);
  DWORD handle = 0;
  DWORD size = GetFileVersionInfoSize(path.getString(), &handle);
  if (size == 0) {
    return L"unknown";
  }
  std::vector<char> data(size);
  TCHAR *text = 0;
  UINT length = 0;
  if (GetFileVersionInfo(path.getString(), handle, size, &data[0]) &&
      VerQueryValue(&data[0], _T("\\StringFileInfo\\040904b0\\ProductVersion"),
                    reinterpret_cast<void **>(&text), &length) && length > 0) {
    return toWide(text);
  }
  return L"unknown";
}

// ---------------------------------------------------------------- Appearance

class AppearanceSection : public SettingsSection
{
public:
  virtual void build(SettingsForm *form)
  {
    m_form = form;
    form->setHeading(SettingsSections::title(SETTINGS_APPEARANCE), kAppearanceSubtitle);
    form->header(L"Theme");
    form->radio(CTL_THEME_AUTO, L"Auto (follow Windows)", true);
    form->radio(CTL_THEME_LIGHT, L"Light", false);
    form->radio(CTL_THEME_DARK, L"Dark", false);
    int theme = ViewerConfig::getInstance()->getTheme();
    check(form->control(CTL_THEME_AUTO + theme), true);
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    if (notification != BN_CLICKED || controlId < CTL_THEME_AUTO || controlId > CTL_THEME_DARK ||
        !isChecked(m_form->control(controlId))) {
      return;
    }
    int theme = controlId - CTL_THEME_AUTO;
    ViewerConfig::getInstance()->setTheme(theme);
    saveViewerConfig();
    // Repaints every window of the viewer with the new palette.
    Theme::setMode(theme);
  }

private:
  SettingsForm *m_form;
};

// ------------------------------------------------- Connection and Input

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

// ------------------------------------------------------------------- Display

class DisplaySection : public SettingsSection
{
public:
  virtual void build(SettingsForm *form)
  {
    m_form = form;
    form->setHeading(SettingsSections::title(SETTINGS_DISPLAY), kDisplaySubtitle);
    form->header(L"Renderer");
    form->radio(CTL_RENDERER_GDI, L"GDI (most compatible)", true);
    form->radio(CTL_RENDERER_AUTO, L"Automatic (GPU when available, GDI otherwise)", false);
    form->radio(CTL_RENDERER_GPU, L"GPU (Direct3D 11, GDI as the fallback)", false);
    form->note(L"Applies to new connections.");
    if (RenderSettings::isOverridden()) {
      form->note(L"This run uses the renderer of the command line (-renderer) or of the "
                 L"FARSIGHTVNC_RENDERER variable; the choice above applies the next time "
                 L"FarSightVNC starts without them.");
    }
    form->header(L"Scaling");
    form->note(L"The remote screen always fits the window, keeping its aspect ratio, in a "
               L"window and in full screen. There are no scroll bars.");
    RenderSettings::Mode mode = RenderSettings::DEFAULT_MODE;
    RenderSettings::readStored(&mode);
    check(form->control(CTL_RENDERER_GDI + mode), true);
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    if (notification != BN_CLICKED || controlId < CTL_RENDERER_GDI ||
        controlId > CTL_RENDERER_GPU || !isChecked(m_form->control(controlId))) {
      return;
    }
    RenderSettings::store(static_cast<RenderSettings::Mode>(controlId - CTL_RENDERER_GDI));
  }

private:
  SettingsForm *m_form;
};

// ------------------------------------------------------------------- General

class GeneralSection : public SettingsSection
{
public:
  explicit GeneralSection(SettingsActions *actions)
  : m_form(0),
    m_actions(actions)
  {
  }

  virtual void build(SettingsForm *form)
  {
    m_form = form;
    ViewerConfig *config = ViewerConfig::getInstance();
    form->setHeading(SettingsSections::title(SETTINGS_GENERAL), kGeneralSubtitle);
    form->header(L"Interface");
    check(form->checkBox(CTL_SHOW_TOOLBAR, L"Show the toolbar in new sessions"),
          config->isToolbarShown());
    check(form->checkBox(CTL_WARN_FULLSCREEN, L"Explain how to leave full screen when entering it"),
          config->isPromptOnFullscreenEnabled());
    form->header(L"History");
    setNumber(form->numberField(CTL_HISTORY_LIMIT, L"Computers to remember", 0, 1024),
              config->getHistoryLimit());
    form->button(CTL_CLEAR_HISTORY, L"Clear history", true);
    form->note(L"Favorites are kept when the history is cleared.");
    form->header(L"Listening mode");
    setNumber(form->numberField(CTL_LISTEN_PORT, L"Port for incoming connections", 1, 65535),
              config->getListenPort());
    form->header(L"Logging");
    setNumber(form->numberField(CTL_LOG_LEVEL, L"Log level (0 turns it off)", 0, 9),
              config->getLogLevel());
    SetWindowTextW(form->textField(CTL_LOG_FILE, L"Log file"), logFileName().c_str());
    form->button(CTL_LOCATE_LOG, L"Locate", true);
    updateLocate();
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    ViewerConfig *config = ViewerConfig::getInstance();
    switch (controlId) {
    case CTL_SHOW_TOOLBAR:
    case CTL_WARN_FULLSCREEN:
      if (notification == BN_CLICKED) {
        config->showToolbar(isChecked(m_form->control(CTL_SHOW_TOOLBAR)));
        config->promptOnFullscreen(isChecked(m_form->control(CTL_WARN_FULLSCREEN)));
        changed();
      }
      break;
    case CTL_HISTORY_LIMIT:
    case CTL_LISTEN_PORT:
    case CTL_LOG_LEVEL:
      // A number applies when the field loses the focus: typing "32" must
      // not cut the history to 3 entries on the way.
      if (notification == EN_KILLFOCUS) {
        commit();
      } else if (notification == EN_CHANGE && controlId == CTL_LOG_LEVEL) {
        updateLocate();
      }
      break;
    case CTL_CLEAR_HISTORY:
      if (notification == BN_CLICKED) {
        config->getConnectionHistory()->load();
        config->getConnectionHistory()->clear();
        m_actions->onViewerConfigChanged();
      }
      break;
    case CTL_LOCATE_LOG:
      if (notification == BN_CLICKED) {
        locateLog();
      }
      break;
    }
  }

  virtual void commit()
  {
    if (m_form == 0) {
      return;
    }
    ViewerConfig *config = ViewerConfig::getInstance();
    bool modified = false;
    int value;
    HWND field = m_form->control(CTL_HISTORY_LIMIT);
    if (getNumber(field, &value) && value != config->getHistoryLimit()) {
      int oldLimit = config->getHistoryLimit();
      config->setHistoryLimit(value);
      if (config->getHistoryLimit() < oldLimit) {
        config->getConnectionHistory()->truncate();
      }
      modified = true;
    }
    setNumber(field, config->getHistoryLimit());
    field = m_form->control(CTL_LISTEN_PORT);
    if (getNumber(field, &value) && value >= 1 && value != config->getListenPort()) {
      config->setListenPort(value);
      modified = true;
    }
    setNumber(field, config->getListenPort());
    field = m_form->control(CTL_LOG_LEVEL);
    if (getNumber(field, &value) && value != config->getLogLevel()) {
      config->setLogLevel(value);
      modified = true;
    }
    setNumber(field, config->getLogLevel());
    if (modified) {
      changed();
    }
  }

private:
  void changed()
  {
    saveViewerConfig();
    m_actions->onViewerConfigChanged();
  }

  std::wstring logFileName() const
  {
    return toWide(ViewerConfig::getInstance()->getPathToLogFile()) + L"\\" +
           toWide(LogNames::VIEWER_LOG_FILE_STUB_NAME) + L".log";
  }

  // "Locate" works when logging is on and the file exists.
  void updateLocate()
  {
    int level = 0;
    bool logging = getNumber(m_form->control(CTL_LOG_LEVEL), &level) && level != 0;
    std::wstring name = logFileName();
    bool exists = GetFileAttributesW(name.c_str()) != INVALID_FILE_ATTRIBUTES;
    EnableWindow(m_form->control(CTL_LOG_FILE), logging);
    EnableWindow(m_form->control(CTL_LOCATE_LOG), logging && exists);
  }

  void locateLog()
  {
    StringStorage command;
    command.format(_T("explorer /select,%s\\%s.log"),
                   ViewerConfig::getInstance()->getPathToLogFile(),
                   LogNames::VIEWER_LOG_FILE_STUB_NAME);
    Process explorer(command.getString());
    try {
      explorer.start();
    } catch (...) {
      MessageBeep(MB_ICONWARNING);
    }
  }

  SettingsForm *m_form;
  SettingsActions *m_actions;
};

// ------------------------------------------------------------------- Updates

class UpdatesSection : public SettingsSection
{
public:
  explicit UpdatesSection(SettingsActions *actions)
  : m_form(0),
    m_actions(actions)
  {
  }

  virtual void build(SettingsForm *form)
  {
    m_form = form;
    UpdateController::Preferences preferences = m_actions->updatePreferences();
    form->setHeading(SettingsSections::title(SETTINGS_UPDATES), kUpdatesSubtitle);
    form->value(L"Installed version", preferences.available && !preferences.currentVersion.empty()
                                        ? preferences.currentVersion.c_str()
                                        : productVersion().c_str());
    form->value(L"Last check", lastCheckText(preferences.lastCheckUnix).c_str());
    form->header(L"Checks");
    HWND autoCheck = form->checkBox(CTL_AUTO_CHECK, L"Check for updates automatically");
    HWND prereleases = form->checkBox(CTL_PRERELEASES, L"Include pre-release versions (alpha and beta)");
    check(autoCheck, preferences.autoCheck);
    check(prereleases, preferences.includePrereleases);
    HWND checkNow = form->button(CTL_CHECK_NOW, L"Check now");
    if (preferences.disabledByPolicy) {
      form->note(L"Update checks are disabled by your administrator.");
    } else if (!preferences.available) {
      form->note(L"This build of FarSightVNC cannot be updated automatically.");
    }
    bool enabled = preferences.available && !preferences.disabledByPolicy;
    EnableWindow(autoCheck, enabled);
    EnableWindow(prereleases, enabled);
    EnableWindow(checkNow, !preferences.disabledByPolicy);
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    if (notification != BN_CLICKED) {
      return;
    }
    switch (controlId) {
    case CTL_AUTO_CHECK:
      m_actions->setUpdateAutoCheck(isChecked(m_form->control(CTL_AUTO_CHECK)));
      break;
    case CTL_PRERELEASES:
      m_actions->setUpdatePrereleases(isChecked(m_form->control(CTL_PRERELEASES)));
      break;
    case CTL_CHECK_NOW:
      m_actions->checkForUpdates();
      break;
    }
  }

private:
  static std::wstring lastCheckText(std::uint64_t unixTime)
  {
    if (unixTime == 0) {
      return L"Never";
    }
    time_t time = static_cast<time_t>(unixTime);
    struct tm local;
    if (localtime_s(&local, &time) != 0) {
      return L"Never";
    }
    wchar_t text[64];
    wcsftime(text, 64, L"%Y-%m-%d %H:%M", &local);
    return text;
  }

  SettingsForm *m_form;
  SettingsActions *m_actions;
};

// --------------------------------------------------------------------- About

class AboutSection : public SettingsSection
{
public:
  virtual void build(SettingsForm *form)
  {
    form->setHeading(SettingsSections::title(SETTINGS_ABOUT), kAboutSubtitle);
    form->value(L"Version", productVersion().c_str());
    form->value(L"Based on", L"TightVNC Viewer 2.8.90");
    form->header(L"License");
    form->note(L"Copyright (C) 2026 FarSightVNC contributors.");
    form->note(L"Copyright (C) 2026 GlavSoft LLC. All Rights Reserved.");
    form->note(kLicense);
    form->header(L"Project");
    form->note(L"Source code: https://github.com/TheSampaio/FarSightVNC");
    form->button(CTL_WEB_SITE, L"Visit the web site");
    form->button(CTL_LICENSE, L"GPL license", true);
    form->header(L"Third-party notices");
    form->note(kLucideSource);
    form->note(kLucideCopyright);
    form->note(kLucidePermission);
    form->note(kLucideDisclaimer);
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    if (notification != BN_CLICKED) {
      return;
    }
    if (controlId == CTL_WEB_SITE) {
      openUrl(IDS_URL_PRODUCT_FVA);
    } else if (controlId == CTL_LICENSE) {
      openUrl(IDS_URL_LICENSING_FVA);
    }
  }
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

std::unique_ptr<SettingsSection> SettingsSections::create(SettingsSectionId section,
                                                          ConnectionConfig *defaults,
                                                          SettingsActions *actions)
{
  switch (section) {
  case SETTINGS_APPEARANCE:
    return std::unique_ptr<SettingsSection>(new AppearanceSection());
  case SETTINGS_CONNECTION:
  case SETTINGS_INPUT:
    return std::unique_ptr<SettingsSection>(new ConnectionOptionsSection(section, defaults));
  case SETTINGS_DISPLAY:
    return std::unique_ptr<SettingsSection>(new DisplaySection());
  case SETTINGS_GENERAL:
    return std::unique_ptr<SettingsSection>(new GeneralSection(actions));
  case SETTINGS_UPDATES:
    return std::unique_ptr<SettingsSection>(new UpdatesSection(actions));
  default:
    return std::unique_ptr<SettingsSection>(new AboutSection());
  }
}
