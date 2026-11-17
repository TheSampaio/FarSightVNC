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
// Modified for FarSightVNC on 2026-11-14: Local shortcuts (keys never sent to the server) in Input.
// Modified for FarSightVNC on 2026-11-14: the Local shortcuts note says that only the bare key is local and that Numpad keys depend on Num Lock.
// Modified for FarSightVNC on 2026-11-16: Add key... takes the key-down through a swallowing keyboard hook (a screenshot tool no longer fires during the capture).
// Modified for FarSightVNC on 2026-11-17: File transfer policy (ask, allow, never) in Input.
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
#include "win-system/WinHooks.h"
#include "../ConnectionOptionsForm.h"
#include "../DefaultConnectionConfig.h"
#include "../NamingDefs.h"
#include "../RenderSettings.h"
#include "../resource.h"

#include <bitset>
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
const wchar_t kFileTransferNote[] =
  L"Ask: the File Transfer window, which lists the remote folders as it opens, and the paste "
  L"of files with Ctrl+V stay off until you confirm once in the session. Allow: they work "
  L"whenever the remote computer supports them. Never: file transfer is not announced to the "
  L"remote computer, so it is unavailable and the paste of files is off.";
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
  CTL_LICENSE,
  CTL_LOCAL_KEYS,
  CTL_LOCAL_ADD,
  CTL_LOCAL_CLEAR,
  CTL_FT_ASK,
  CTL_FT_ALLOW,
  CTL_FT_NEVER
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

// ------------------------------------------------------------- Local keys

// Name of a local key ("Num -", "Insert"), from the keyboard layout.
std::wstring localKeyName(unsigned index)
{
  unsigned vk = index & 0xff;
  bool extended = (index & 0x100) != 0;
  UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  wchar_t name[64] = L"";
  wchar_t plain[64] = L"";
  LONG lParam = static_cast<LONG>(scan << 16);
  if (scan == 0 || GetKeyNameTextW(lParam | (extended ? 0x1000000 : 0), name, 64) <= 0) {
    swprintf_s(name, L"Key %02X", vk);
  }
  std::wstring result(name);
  // The same name for the extended and the plain key: tell them apart.
  if (extended && scan != 0 && GetKeyNameTextW(lParam, plain, 64) > 0 && result == plain) {
    result += L" (ext)";
  }
  return result;
}

// Keys that cannot be local: modifiers (shortcuts) and the keys that always
// stay with Windows (lock keys).
bool canBeLocalKey(unsigned vk)
{
  switch (vk) {
  case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
  case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
  case VK_MENU: case VK_LMENU: case VK_RMENU:
  case VK_LWIN: case VK_RWIN:
  case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
  case VK_PROCESSKEY: case VK_PACKET: case 0: case 0xFF:
    return false;
  }
  return true;
}

// "Local shortcuts" rows of the Input section: the list of local keys,
// "Add key..." that takes the next key pressed and "Clear". The keys are
// app-wide (ViewerConfig), saved and applied to the keyboard hook at once.
//
// "Add key..." arms a WinHooks::KeyCapture: a low level keyboard hook that
// takes the next key-down and swallows it. The window messages cannot be used:
// a screenshot tool (Lightshot, the Snipping Tool) registers its key as a
// system hot key, so it fires, and its overlay takes the focus, before the
// key reaches any window. The hook posts the key to the "Add key..." button.
// It is removed when the key was taken (after the key-up), on Esc, when the
// button loses the focus (which also covers another application taking the
// foreground), when the button is destroyed and after kCaptureTimeoutMs.
class LocalKeysEditor
{
public:
  LocalKeysEditor()
  : m_form(0),
    m_addButton(0),
    m_capturing(false)
  {
  }

  ~LocalKeysEditor()
  {
    if (m_addButton != 0 && IsWindow(m_addButton)) {
      RemoveWindowSubclass(m_addButton, captureProc, kSubclassId);
    }
    // m_capture removes the hook if it is still armed.
  }

  void build(SettingsForm *form)
  {
    m_form = form;
    form->header(L"Local shortcuts");
    form->note(L"Keys listed here go to Windows and the applications of this computer "
               L"(a screenshot tool, for example) and are never sent to the remote "
               L"computer, in every session. Only the key alone is local: with Ctrl, Alt "
               L"or Shift held the modifier still goes to the remote computer. Numpad keys "
               L"depend on Num Lock. Print Screen is local by default and can be removed "
               L"with Clear, then added again with Add key.");
    form->textField(CTL_LOCAL_KEYS, L"Local keys");
    m_addButton = form->button(CTL_LOCAL_ADD, L"Add key…");
    form->button(CTL_LOCAL_CLEAR, L"Clear", true);
    if (m_addButton != 0) {
      SetWindowSubclass(m_addButton, captureProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
    }
    StringStorage stored;
    ViewerConfig::getInstance()->getLocalKeys(&stored);
    m_keys = WinHooks::parseLocalKeys(stored.getString());
    showKeys();
  }

  // True when the command was one of these rows.
  bool onCommand(UINT controlId, UINT notification)
  {
    if (controlId != CTL_LOCAL_ADD && controlId != CTL_LOCAL_CLEAR && controlId != CTL_LOCAL_KEYS) {
      return false;
    }
    if (notification != BN_CLICKED) {
      return true;
    }
    if (controlId == CTL_LOCAL_ADD) {
      startCapture();
    } else if (controlId == CTL_LOCAL_CLEAR) {
      cancelCapture();
      m_keys.reset();
      apply();
    }
    return true;
  }

private:
  static const UINT_PTR kSubclassId = 0x4C4B;   // Not the form's own subclass ID.
  // Posted by the capture hook to the button: wParam is the virtual key
  // (0: the capture timed out), lParam bit 0 the extended flag.
  static const UINT kCaptureMessage = WM_APP + 0x4B;
  // A capture that is never answered ends by itself, so the global keyboard
  // hook cannot stay installed.
  static const DWORD kCaptureTimeoutMs = 10000;

  void startCapture()
  {
    // The Enter that pressed the button is already down: the hook ignores the
    // keys Windows has down, so no snapshot of them is needed.
    if (!m_capture.arm(m_addButton, kCaptureMessage, kCaptureTimeoutMs)) {
      MessageBeep(MB_ICONWARNING);
      return;
    }
    m_capturing = true;
    // The focus loss of the button ends the capture.
    SetFocus(m_addButton);
    SetWindowTextW(m_form->control(CTL_LOCAL_KEYS), L"Press a key… (Esc to cancel)");
  }

  // Ends the capture from outside (focus lost, Clear, button destroyed):
  // removes the hook at once.
  void cancelCapture()
  {
    m_capture.disarm();
    endCapture();
  }

  // Back to the list of keys. The hook stays until the key-up of the key it
  // took, so that no other application sees that key-up alone.
  void endCapture()
  {
    if (m_capturing) {
      m_capturing = false;
      showKeys();
    }
  }

  // The message of the hook: the key taken (or 0 on timeout), already
  // swallowed. A message of a capture that already ended is ignored.
  void onCaptureMessage(unsigned vk, bool extended)
  {
    if (!m_capturing) {
      return;
    }
    m_capturing = false;
    if (vk == 0 || vk == VK_ESCAPE) {
      showKeys();
    } else if (canBeLocalKey(vk)) {
      m_keys.set(WinHooks::localKeyIndex(vk, extended));
      apply();
    } else {
      MessageBeep(MB_ICONWARNING);
      showKeys();
    }
  }

  void apply()
  {
    StringStorage text = WinHooks::formatLocalKeys(m_keys);
    ViewerConfig::getInstance()->setLocalKeys(text.getString());
    saveViewerConfig();
    WinHooks::setLocalKeys(m_keys);
    showKeys();
  }

  void showKeys()
  {
    std::wstring text;
    for (unsigned i = 0; i < m_keys.size(); i++) {
      if (!m_keys.test(i)) continue;
      if (!text.empty()) text += L", ";
      text += localKeyName(i);
    }
    if (text.empty()) {
      text = L"None";
    }
    SetWindowTextW(m_form->control(CTL_LOCAL_KEYS), text.c_str());
  }

  static LRESULT CALLBACK captureProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR id, DWORD_PTR data)
  {
    LocalKeysEditor *editor = reinterpret_cast<LocalKeysEditor *>(data);
    switch (message) {
    case kCaptureMessage:
      editor->onCaptureMessage(static_cast<unsigned>(wParam), (lParam & 1) != 0);
      return 0;
    case WM_KILLFOCUS:
      editor->cancelCapture();
      break;
    case WM_NCDESTROY:
      // The form is going away: no row left to restore.
      editor->m_capture.disarm();
      editor->m_capturing = false;
      RemoveWindowSubclass(hwnd, captureProc, id);
      editor->m_addButton = 0;
      break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
  }

  SettingsForm *m_form;
  HWND m_addButton;
  bool m_capturing;
  WinHooks::LocalKeySet m_keys;
  WinHooks::KeyCapture m_capture;
};

// ------------------------------------------------- Connection and Input

// The connection options of one section, applied to the defaults of new
// connections and saved at every change.
class ConnectionOptionsSection : public SettingsSection
{
public:
  ConnectionOptionsSection(SettingsSectionId id, ConnectionConfig *defaults)
  : m_id(id),
    m_form(0),
    m_defaults(defaults)
  {
  }

  virtual void build(SettingsForm *form)
  {
    m_form = form;
    if (m_id == SETTINGS_CONNECTION) {
      buildConnection(form);
    } else {
      buildInput(form);
    }
    m_options.bind([form](UINT controlId) { return form->control(controlId); });
    m_options.load(m_defaults, false);
    if (m_id == SETTINGS_INPUT) {
      // Not part of ConnectionOptionsForm, which the Options dialog shares.
      check(form->control(CTL_FT_ASK + m_defaults->getFileTransferPolicy()), true);
    }
  }

  virtual void onCommand(UINT controlId, UINT notification)
  {
    if (m_localKeys.onCommand(controlId, notification)) {
      return;
    }
    if (controlId >= CTL_FT_ASK && controlId <= CTL_FT_NEVER) {
      if (notification == BN_CLICKED && isChecked(m_form->control(controlId))) {
        m_defaults->setFileTransferPolicy(controlId - CTL_FT_ASK);
        save();
      }
      return;
    }
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
    form->header(L"File transfer");
    form->radio(CTL_FT_ASK, L"Ask once per session before using it (recommended)", true);
    form->radio(CTL_FT_ALLOW, L"Allow, without asking", false);
    form->radio(CTL_FT_NEVER, L"Never (not offered to the remote computer)", false);
    form->note(kFileTransferNote);
    form->header(L"Mouse");
    form->checkBox(IDC_CSWAPMOUSE, L"Swap the right and middle buttons");
    m_localKeys.build(form);
  }

  void save()
  {
    m_options.save(m_defaults);
    DefaultConnectionConfig::save(m_defaults);
  }

  SettingsSectionId m_id;
  SettingsForm *m_form;
  ConnectionConfig *m_defaults;
  ConnectionOptionsForm m_options;
  // Input only.
  LocalKeysEditor m_localKeys;
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
