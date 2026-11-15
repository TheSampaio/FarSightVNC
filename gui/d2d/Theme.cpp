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

// DWMWA_USE_IMMERSIVE_DARK_MODE is declared for Windows 10 (dwmapi.h);
// util/winhdr.h would fix WINVER 0x0501 and hide it, so this file sets the
// version before any header, as d2d/SvgIcons.cpp does. Everything it needs at
// run time is resolved with GetProcAddress(), so the viewer still starts on
// the older Windows versions it supports.
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0A00
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00
#ifdef NTDDI_VERSION
#undef NTDDI_VERSION
#endif
#define NTDDI_VERSION 0x0A000003

#include <windows.h>
#include <string.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <string>

#include "Theme.h"

namespace {

// Windows 10 1809: the first build with dark mode for the windows of an
// application. 1903 changed the undocumented entry point of uxtheme.dll.
const DWORD kBuildDarkMode = 17763;
const DWORD kBuild1903 = 18362;
// Before this build DWMWA_USE_IMMERSIVE_DARK_MODE had the attribute 19.
const DWORD kBuildDarkModeAttribute = 18985;

const DWORD kAttributeDarkMode = 20;
const DWORD kAttributeDarkModeOld = 19;

// SetPreferredAppMode() of 1903 and later.
enum PreferredAppMode { APP_MODE_FORCE_DARK = 2, APP_MODE_FORCE_LIGHT = 3 };

typedef HRESULT (WINAPI *DwmSetWindowAttributeProc)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *SetWindowThemeProc)(HWND, LPCWSTR, LPCWSTR);
typedef int (WINAPI *SetPreferredAppModeProc)(int);
typedef bool (WINAPI *AllowDarkModeForAppProc)(bool);
typedef bool (WINAPI *AllowDarkModeForWindowProc)(HWND, bool);
typedef void (WINAPI *FlushMenuThemesProc)();
typedef LONG (WINAPI *RtlGetVersionProc)(RTL_OSVERSIONINFOW *);
typedef HTHEME (WINAPI *OpenThemeDataProc)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *CloseThemeDataProc)(HTHEME);
typedef HRESULT (WINAPI *GetThemePartSizeProc)(HTHEME, HDC, int, int, LPCRECT, THEMESIZE, SIZE *);
typedef HRESULT (WINAPI *DrawThemeBackgroundProc)(HTHEME, HDC, int, int, LPCRECT, LPCRECT);
typedef HRESULT (WINAPI *DrawThemeTextExProc)(HTHEME, HDC, int, int, LPCWSTR, int, DWORD,
                                              LPRECT, const DTTOPTS *);

// Entry points of dwmapi.dll and uxtheme.dll, resolved once. They are loaded
// at run time so that no target linking gui needs new link libraries, and
// every one of them may be missing on an older Windows: the theme then only
// loses the dark frame and the dark non-client parts of the controls, since
// the WM_CTLCOLOR* handlers of the windows always paint the themed colors.
struct ThemeApi
{
  ThemeApi()
  : dwmSetWindowAttribute(0),
    setWindowTheme(0),
    setPreferredAppMode(0),
    allowDarkModeForApp(0),
    allowDarkModeForWindow(0),
    flushMenuThemes(0),
    openThemeData(0),
    closeThemeData(0),
    getThemePartSize(0),
    drawThemeBackground(0),
    drawThemeTextEx(0),
    build(0)
  {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionProc rtlGetVersion = ntdll != 0
      ? reinterpret_cast<RtlGetVersionProc>(
          reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")))
      : 0;
    if (rtlGetVersion != 0) {
      RTL_OSVERSIONINFOW version;
      ZeroMemory(&version, sizeof(version));
      version.dwOSVersionInfoSize = sizeof(version);
      if (rtlGetVersion(&version) == 0) {
        build = version.dwBuildNumber;
      }
    }
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (dwmapi != 0) {
      dwmSetWindowAttribute = reinterpret_cast<DwmSetWindowAttributeProc>(
        reinterpret_cast<void *>(GetProcAddress(dwmapi, "DwmSetWindowAttribute")));
    }
    HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    if (uxtheme == 0) {
      return;
    }
    setWindowTheme = reinterpret_cast<SetWindowThemeProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, "SetWindowTheme")));
    // Documented, but resolved here as well so that no target linking gui
    // needs the uxtheme import library (customDrawButton()).
    openThemeData = reinterpret_cast<OpenThemeDataProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, "OpenThemeData")));
    closeThemeData = reinterpret_cast<CloseThemeDataProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, "CloseThemeData")));
    getThemePartSize = reinterpret_cast<GetThemePartSizeProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, "GetThemePartSize")));
    drawThemeBackground = reinterpret_cast<DrawThemeBackgroundProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, "DrawThemeBackground")));
    drawThemeTextEx = reinterpret_cast<DrawThemeTextExProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, "DrawThemeTextEx")));
    // Ordinals 133, 135 and 136 of uxtheme.dll are undocumented, but they are
    // what every Win32 application uses to get dark common controls: 133 is
    // AllowDarkModeForWindow(HWND, bool), 135 is SetPreferredAppMode(int)
    // since 1903 and AllowDarkModeForApp(bool) in 1809, 136 is
    // FlushMenuThemes().
    if (build < kBuildDarkMode) {
      return;
    }
    allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133))));
    FARPROC ordinal135 = GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    if (build >= kBuild1903) {
      setPreferredAppMode =
        reinterpret_cast<SetPreferredAppModeProc>(reinterpret_cast<void *>(ordinal135));
    } else {
      allowDarkModeForApp =
        reinterpret_cast<AllowDarkModeForAppProc>(reinterpret_cast<void *>(ordinal135));
    }
    flushMenuThemes = reinterpret_cast<FlushMenuThemesProc>(
      reinterpret_cast<void *>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))));
  }

  DwmSetWindowAttributeProc dwmSetWindowAttribute;
  SetWindowThemeProc setWindowTheme;
  SetPreferredAppModeProc setPreferredAppMode;
  AllowDarkModeForAppProc allowDarkModeForApp;
  AllowDarkModeForWindowProc allowDarkModeForWindow;
  FlushMenuThemesProc flushMenuThemes;
  OpenThemeDataProc openThemeData;
  CloseThemeDataProc closeThemeData;
  GetThemePartSizeProc getThemePartSize;
  DrawThemeBackgroundProc drawThemeBackground;
  DrawThemeTextExProc drawThemeTextEx;
  DWORD build;
};

const ThemeApi &api()
{
  static ThemeApi loaded;
  return loaded;
}

// The Windows app theme: HKCU AppsUseLightTheme, where a missing or non-zero
// value means light.
bool systemIsDark()
{
  HKEY key = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }
  DWORD value = 1;
  DWORD size = sizeof(value);
  DWORD type = 0;
  bool dark = false;
  if (RegQueryValueExW(key, L"AppsUseLightTheme", 0, &type,
                       reinterpret_cast<BYTE *>(&value), &size) == ERROR_SUCCESS &&
      type == REG_DWORD) {
    dark = value == 0;
  }
  RegCloseKey(key);
  return dark;
}

COLORREF toColorRef(ThemeColor color)
{
  return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

int g_mode = Theme::MODE_AUTO;
bool g_dark = false;

bool evaluate(int mode)
{
  if (mode == Theme::MODE_LIGHT) {
    return false;
  }
  if (mode == Theme::MODE_DARK) {
    return true;
  }
  return systemIsDark();
}

// Tells the common controls which theme to draw: our effective theme, which
// is not the system one when the user forced Light or Dark.
void applyProcessDarkMode()
{
  const ThemeApi &entries = api();
  if (entries.setPreferredAppMode != 0) {
    entries.setPreferredAppMode(g_dark ? APP_MODE_FORCE_DARK : APP_MODE_FORCE_LIGHT);
  } else if (entries.allowDarkModeForApp != 0) {
    entries.allowDarkModeForApp(g_dark);
  }
  if (entries.flushMenuThemes != 0) {
    entries.flushMenuThemes();
  }
}

struct BroadcastContext
{
  DWORD processId;
  UINT message;
};

BOOL CALLBACK sendToChild(HWND child, LPARAM parameter)
{
  const BroadcastContext *context = reinterpret_cast<const BroadcastContext *>(parameter);
  DWORD_PTR result = 0;
  SendMessageTimeout(child, context->message, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 1000,
                     &result);
  return TRUE;
}

BOOL CALLBACK sendToTopLevel(HWND window, LPARAM parameter)
{
  const BroadcastContext *context = reinterpret_cast<const BroadcastContext *>(parameter);
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  if (processId != context->processId) {
    return TRUE;
  }
  sendToChild(window, parameter);
  EnumChildWindows(window, sendToChild, parameter);
  RedrawWindow(window, 0, 0,
               RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
  return TRUE;
}

}

namespace Theme {

UINT changedMessage()
{
  static UINT message = RegisterWindowMessageW(L"FarSightVNCThemeChanged");
  return message;
}

int mode()
{
  return g_mode;
}

bool isDark()
{
  return g_dark;
}

const ThemeTokens &tokens()
{
  return g_dark ? kDarkTheme : kLightTheme;
}

void setMode(int mode)
{
  g_mode = mode;
  bool dark = evaluate(mode);
  if (dark == g_dark) {
    return;
  }
  g_dark = dark;
  applyProcessDarkMode();
  broadcast();
}

bool refreshSystem()
{
  bool dark = evaluate(g_mode);
  if (dark == g_dark) {
    return false;
  }
  g_dark = dark;
  applyProcessDarkMode();
  return true;
}

void broadcast()
{
  BroadcastContext context;
  context.processId = GetCurrentProcessId();
  context.message = changedMessage();
  EnumWindows(sendToTopLevel, reinterpret_cast<LPARAM>(&context));
}

void applyWindowDarkMode(HWND hwnd)
{
  const ThemeApi &entries = api();
  if (entries.dwmSetWindowAttribute == 0 || hwnd == 0 || entries.build < kBuildDarkMode) {
    return;
  }
  BOOL dark = g_dark ? TRUE : FALSE;
  DWORD attribute = entries.build >= kBuildDarkModeAttribute ? kAttributeDarkMode
                                                             : kAttributeDarkModeOld;
  entries.dwmSetWindowAttribute(hwnd, attribute, &dark, sizeof(dark));
}

void applyControlTheme(HWND control)
{
  const ThemeApi &entries = api();
  if (control == 0) {
    return;
  }
  // The window must be allowed into dark mode before it looks for its theme
  // data, or it keeps the light one.
  if (entries.allowDarkModeForWindow != 0) {
    entries.allowDarkModeForWindow(control, g_dark);
  }
  wchar_t className[32] = { 0 };
  GetClassNameW(control, className, sizeof(className) / sizeof(className[0]));
  const wchar_t *name;
  if (_wcsicmp(className, L"BUTTON") == 0) {
    // A themed BUTTON draws its own label with DrawThemeText and ignores the
    // color of WM_CTLCOLORBTN/WM_CTLCOLORSTATIC, so the check boxes and the
    // radio buttons need the dark Button theme itself: with ordinal 133
    // applied, plain "Explorer" resolves to it. "DarkMode_Explorer" would
    // not, and the labels would stay dark gray on the dark background.
    name = L"Explorer";
  } else if (_wcsicmp(className, L"COMBOBOX") == 0 || _wcsicmp(className, L"EDIT") == 0) {
    // The combo box and field variant (CFD), which themes the drop-down
    // button and the border of the box.
    name = g_dark ? L"DarkMode_CFD" : L"CFD";
  } else {
    // Track bars, spin controls, labels, list boxes and the pages.
    name = g_dark ? L"DarkMode_Explorer" : L"Explorer";
  }
  if (entries.setWindowTheme != 0) {
    entries.setWindowTheme(control, name, 0);
  }
  SendMessage(control, WM_THEMECHANGED, 0, 0);
}

bool customDrawButton(HWND button, void *nmcustomdraw)
{
  const ThemeApi &entries = api();
  NMCUSTOMDRAW *custom = static_cast<NMCUSTOMDRAW *>(nmcustomdraw);
  if (!g_dark || button == 0 || custom == 0 || custom->dwDrawStage != CDDS_PREPAINT ||
      entries.openThemeData == 0 || entries.closeThemeData == 0 ||
      entries.getThemePartSize == 0 || entries.drawThemeBackground == 0 ||
      entries.drawThemeTextEx == 0) {
    return false;
  }
  LONG style = GetWindowLong(button, GWL_STYLE) & BS_TYPEMASK;
  bool radio = style == BS_RADIOBUTTON || style == BS_AUTORADIOBUTTON;
  bool box = style == BS_CHECKBOX || style == BS_AUTOCHECKBOX ||
             style == BS_3STATE || style == BS_AUTO3STATE;
  if (!radio && !box) {
    return false;
  }
  HTHEME theme = entries.openThemeData(button, L"Button");
  if (theme == 0) {
    return false;
  }

  // The parts of a check box and of a radio button have parallel states:
  // normal, hot, pressed and disabled, four by four, from unchecked (1) to
  // checked (5) and, for a check box only, mixed (9).
  int part = radio ? BP_RADIOBUTTON : BP_CHECKBOX;
  LRESULT checked = SendMessage(button, BM_GETCHECK, 0, 0);
  int base = checked == BST_CHECKED ? 5 : (checked == BST_INDETERMINATE ? 9 : 1);
  int offset = 0;
  if ((custom->uItemState & CDIS_DISABLED) != 0) {
    offset = 3;
  } else if ((custom->uItemState & CDIS_SELECTED) != 0) {
    offset = 2;
  } else if ((custom->uItemState & CDIS_HOT) != 0) {
    offset = 1;
  }
  int state = base + offset;

  HDC dc = custom->hdc;
  HBRUSH background = CreateSolidBrush(toColorRef(tokens().background));
  if (background != 0) {
    FillRect(dc, &custom->rc, background);
    DeleteObject(background);
  }
  HFONT font = reinterpret_cast<HFONT>(SendMessage(button, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font != 0 ? SelectObject(dc, font) : 0;

  SIZE glyph = { 0, 0 };
  entries.getThemePartSize(theme, dc, part, state, 0, TS_TRUE, &glyph);
  RECT box2 = custom->rc;
  box2.top += ((box2.bottom - box2.top) - glyph.cy) / 2;
  box2.bottom = box2.top + glyph.cy;
  box2.right = box2.left + glyph.cx;
  entries.drawThemeBackground(theme, dc, part, state, &box2, 0);

  // The label, with our own color: the Button theme would draw it in a tone
  // meant for a light background (dark gray on dark for every state but the
  // checked one).
  RECT text = custom->rc;
  text.left = box2.right + glyph.cx / 3;
  if (text.left < text.right) {
    wchar_t label[256] = { 0 };
    GetWindowTextW(button, label, sizeof(label) / sizeof(label[0]));
    DWORD flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_WORD_ELLIPSIS;
    LRESULT uiState = SendMessage(button, WM_QUERYUISTATE, 0, 0);
    if ((uiState & UISF_HIDEACCEL) != 0) {
      flags |= DT_HIDEPREFIX;
    }
    DTTOPTS options;
    ZeroMemory(&options, sizeof(options));
    options.dwSize = sizeof(options);
    options.dwFlags = DTT_TEXTCOLOR;
    options.crText = toColorRef((custom->uItemState & CDIS_DISABLED) != 0
                                ? tokens().disabledText : tokens().text);
    entries.drawThemeTextEx(theme, dc, part, state, label, -1, flags, &text, &options);
    if ((custom->uItemState & CDIS_FOCUS) != 0 && (uiState & UISF_HIDEFOCUS) == 0) {
      DrawFocusRect(dc, &text);
    }
  }

  if (oldFont != 0) {
    SelectObject(dc, oldFont);
  }
  entries.closeThemeData(theme);
  return true;
}

void applyControlThemeTree(HWND parent)
{
  for (HWND child = GetWindow(parent, GW_CHILD); child != 0;
       child = GetWindow(child, GW_HWNDNEXT)) {
    applyControlTheme(child);
  }
}

}
