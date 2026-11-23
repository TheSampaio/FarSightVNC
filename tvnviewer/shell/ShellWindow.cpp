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
// Modified for FarSightVNC on 2026-11-12: "+" new connection button of the title bar (client button, tooltip).
// Modified for FarSightVNC on 2026-11-22: the tabs are an fsv::shell::TabModel (any number of tabs, active index).
// Modified for FarSightVNC on 2026-11-23: browser-style tabs (title bar tab strip, per-tab activation, closing and reconnection).
// Modified for FarSightVNC on 2026-11-23: tab keyboard shortcuts (onShortcutKey()).
//

#include "ShellWindow.h"

#include "gui/d2d/Theme.h"
#include "HomeTab.h"
#include "ReconnectTab.h"
#include "ShellShortcuts.h"
#include "../ViewerInstance.h"

#include "gui/DpiUtils.h"
#include "../NamingDefs.h"
#include "../resource.h"

#include <CommCtrl.h>

#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER 92
#endif
#ifndef WM_NCMOUSELEAVE
#define WM_NCMOUSELEAVE 0x02A2
#endif

namespace {

// Window property that holds the ShellWindow of frameProc().
const TCHAR kShellProperty[] = _T("FarSightVNC.ShellWindow");
// Height of the top resize edge inside the title bar, in DIPs.
const int kTopResizeEdge = 5;
// Title of the Home tab and size of the content of a new Home window, in
// DIPs.
const TCHAR kHomeTitle[] = _T("Home");
// Title of the active tab while the Settings page covers it.
const wchar_t kSettingsTitle[] = L"Settings";
// Mark of the items the shell copies into its system menu (dwItemData).
const ULONG_PTR kSessionMenuTag = 0x46535643;
// Section of the Settings page shown last in this process: the settings
// button opens it again.
SettingsSectionId s_lastSection = SETTINGS_APPEARANCE;
const float kHomeWidth = 900.0f;
const float kHomeHeight = 640.0f;

// Tooltips: the "+" button, and the title of the tab under the pointer when
// it is cut.
const wchar_t kNewTabTooltip[] = L"New tab (Ctrl+T)";
const UINT_PTR kNewTabToolId = 1;
const UINT_PTR kTabToolId = 2;

// MARGINS of uxtheme.h, declared here because util/winhdr.h hides the DWM
// API (WINVER 0x0501); DwmExtendFrameIntoClientArea is loaded at run time.
struct FrameMargins
{
  int leftWidth;
  int rightWidth;
  int topHeight;
  int bottomHeight;
};
typedef HRESULT (WINAPI *DwmExtendFrameIntoClientAreaFunc)(HWND, const FrameMargins *);

DwmExtendFrameIntoClientAreaFunc dwmExtendFrameIntoClientArea()
{
  static bool loaded = false;
  static DwmExtendFrameIntoClientAreaFunc function = 0;
  if (!loaded) {
    loaded = true;
    HMODULE dwmapi = LoadLibrary(_T("dwmapi.dll"));
    if (dwmapi != 0) {
      function = reinterpret_cast<DwmExtendFrameIntoClientAreaFunc>(
        GetProcAddress(dwmapi, "DwmExtendFrameIntoClientArea"));
    }
  }
  return function;
}

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

bool isWindowButton(TitleBarPart part)
{
  return part == TITLE_BAR_MINIMIZE || part == TITLE_BAR_MAXIMIZE || part == TITLE_BAR_CLOSE;
}

// Parts in the client area of the shell (the others are non-client).
bool isClientButton(TitleBarPart part)
{
  return part == TITLE_BAR_SETTINGS || part == TITLE_BAR_NEW_TAB || part == TITLE_BAR_TAB ||
         part == TITLE_BAR_TAB_CLOSE;
}

// Appends the items of source to target (position -1) or inserts them from
// position; submenus are copied. The items of the top level carry
// kSessionMenuTag.
void copyMenuItems(HMENU source, HMENU target, int position, bool tag)
{
  int count = GetMenuItemCount(source);
  for (int i = 0; i < count; i++) {
    TCHAR text[256];
    text[0] = 0;
    MENUITEMINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU | MIIM_STRING;
    info.dwTypeData = text;
    info.cch = sizeof(text) / sizeof(text[0]) - 1;
    if (!GetMenuItemInfo(source, i, TRUE, &info)) {
      continue;
    }
    MENUITEMINFO copy;
    ZeroMemory(&copy, sizeof(copy));
    copy.cbSize = sizeof(copy);
    copy.fMask = MIIM_FTYPE | MIIM_DATA;
    copy.fType = info.fType & MFT_SEPARATOR;
    copy.dwItemData = tag ? kSessionMenuTag : 0;
    if (!(info.fType & MFT_SEPARATOR)) {
      copy.fMask |= MIIM_ID | MIIM_STATE | MIIM_STRING;
      copy.wID = info.wID;
      copy.fState = info.fState;
      copy.dwTypeData = text;
      if (info.hSubMenu != 0) {
        HMENU submenu = CreatePopupMenu();
        copyMenuItems(info.hSubMenu, submenu, -1, false);
        copy.fMask |= MIIM_SUBMENU;
        copy.hSubMenu = submenu;
      }
    }
    int where = position < 0 ? GetMenuItemCount(target) : position++;
    InsertMenuItem(target, where, TRUE, &copy);
  }
}

}

ShellWindow::ShellWindow(const TCHAR *windowClassName, Logger *logger, HomeActions *home,
                         SettingsActions *settings)
: m_logWriter(logger),
  m_home(home),
  m_settingsActions(settings),
  m_stagedReplaceId(0),
  m_nextTabId(1),
  m_middleTabId(0),
  m_lastFocus(0),
  m_destroyed(false),
  m_sessionWasShown(false),
  m_isFullscreen(false),
  m_baseProc(0),
  m_tooltip(0),
  m_frameActive(false),
  m_trackingMouse(false),
  m_trackingNcMouse(false)
{
  ZeroMemory(&m_windowedPlacement, sizeof(m_windowedPlacement));
  m_windowedPlacement.length = sizeof(m_windowedPlacement);

  StringStorage windowClass(windowClassName);
  StringStorage title(WindowNames::TVN_WINDOW_TITLE_NAME);
  setClass(&windowClass);
  createWindow(&title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
  loadIcon(IDI_APPICON);
  if (m_hWnd == 0) {
    return;
  }
  SetProp(m_hWnd, kShellProperty, static_cast<HANDLE>(this));
  m_baseProc = reinterpret_cast<WNDPROC>(
    SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(frameProc)));
  m_chrome.attach(m_hWnd, DpiUtils::getDpiForWindow(m_hWnd));
  // The border of the frame follows the theme (the caption is ours).
  Theme::applyWindowDarkMode(m_hWnd);
  createTooltip();
  updateDwmFrame();
  // Recalculates the frame without the caption (WM_NCCALCSIZE).
  SetWindowPos(m_hWnd, 0, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

ShellWindow::~ShellWindow()
{
  // The tabs reference sessions owned elsewhere: never call them from here.
  m_settings.reset();
  m_staged.reset();
  m_tabs.clear();
  // ~BaseWindow destroys a window that still exists: its messages must not
  // reach this object any more.
  restoreBaseProc();
}

void ShellWindow::restoreBaseProc()
{
  if (m_hWnd != 0 && m_baseProc != 0) {
    SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_baseProc));
    RemoveProp(m_hWnd, kShellProperty);
  }
}

LRESULT CALLBACK ShellWindow::frameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  ShellWindow *shell = static_cast<ShellWindow *>(GetProp(hwnd, kShellProperty));
  if (shell == 0 || shell->m_baseProc == 0) {
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  LRESULT result = 0;
  if (shell->onFrameMessage(message, wParam, lParam, &result)) {
    return result;
  }
  WNDPROC baseProc = shell->m_baseProc;
  if (message == WM_NCDESTROY) {
    // The last message of the window: the class procedure handles it (and
    // marks the shell as destroyed) without this procedure.
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(baseProc));
    RemoveProp(hwnd, kShellProperty);
  }
  return CallWindowProc(baseProc, hwnd, message, wParam, lParam);
}

LRESULT ShellWindow::callBaseProc(UINT message, WPARAM wParam, LPARAM lParam)
{
  return CallWindowProc(m_baseProc, m_hWnd, message, wParam, lParam);
}

bool ShellWindow::onFrameMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *result)
{
  if (message == SettingsPage::closeMessage()) {
    // Back or Esc on the Settings page (posted by the page).
    closeSettings();
    *result = 0;
    return true;
  }
  // Not a constant (RegisterWindowMessage), so it cannot be a case label.
  if (message == Theme::changedMessage()) {
    Theme::applyWindowDarkMode(m_hWnd);
    InvalidateRect(m_hWnd, 0, TRUE);
    *result = 0;
    return true;
  }
  if (m_tooltip != 0 && message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
    MSG relay = { m_hWnd, message, wParam, lParam, 0, { 0, 0 } };
    SendMessage(m_tooltip, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relay));
  }
  switch (message) {
  case WM_NCCALCSIZE:
    if (wParam == TRUE && !m_isFullscreen) {
      // The default frame minus the caption: the title bar is part of the
      // client area, the resize borders stay. A maximized window extends
      // past its monitor by the frame on every side, so the top of its
      // client area moves down by that much to stay on screen.
      NCCALCSIZE_PARAMS *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam);
      LONG windowTop = params->rgrc[0].top;
      *result = callBaseProc(message, wParam, lParam);
      params->rgrc[0].top = windowTop;
      if (IsZoomed(m_hWnd)) {
        params->rgrc[0].top += maximizedFrameThickness();
      }
      return true;
    }
    return false;
  case WM_NCHITTEST:
    if (m_isFullscreen) {
      return false;
    }
    *result = hitTest(wParam, lParam);
    return true;
  case WM_NCMOUSEMOVE:
  {
    // DefWindowProc would track and paint the classic caption buttons: the
    // title bar handles them.
    TitleBarPart part = windowButtonFromHitCode(wParam);
    trackMouseLeave(true);
    if (m_titleBar.setHover(part)) {
      invalidateTitleBar();
    }
    if (part != TITLE_BAR_NONE) {
      *result = 0;
      return true;
    }
    return false;
  }
  case WM_NCMOUSELEAVE:
    m_trackingNcMouse = false;
    if (isWindowButton(m_titleBar.pressed())) {
      m_titleBar.setPressed(TITLE_BAR_NONE);
      invalidateTitleBar();
    }
    if (isWindowButton(m_titleBar.hover()) && m_titleBar.setHover(TITLE_BAR_NONE)) {
      invalidateTitleBar();
    }
    *result = 0;
    return true;
  case WM_NCLBUTTONDOWN:
  case WM_NCLBUTTONDBLCLK:
  {
    TitleBarPart part = windowButtonFromHitCode(wParam);
    if (part == TITLE_BAR_NONE) {
      return false;
    }
    m_titleBar.setPressed(part);
    m_titleBar.setHover(part);
    invalidateTitleBar();
    *result = 0;
    return true;
  }
  case WM_NCLBUTTONUP:
  {
    TitleBarPart pressed = m_titleBar.pressed();
    if (!isWindowButton(pressed)) {
      return false;
    }
    m_titleBar.setPressed(TITLE_BAR_NONE);
    invalidateTitleBar();
    if (windowButtonFromHitCode(wParam) == pressed) {
      executeTitleBarPart(pressed, TitleBar::kNoTab);
    }
    *result = 0;
    return true;
  }
  case WM_INITMENUPOPUP:
    // The system menu shows the session menu of the active tab.
    if (HIWORD(lParam) != 0) {
      refreshSystemMenu();
    }
    return false;
  case WM_MOUSEMOVE:
  {
    // The tabs and the settings and "+" buttons of the bar are in the client
    // area; the rest of the client area belongs to the session view.
    size_t tab;
    TitleBarPart part = partAtClientPoint(lParam, &tab);
    if (isClientButton(part)) {
      trackMouseLeave(false);
    }
    if (m_titleBar.setHover(isClientButton(part) ? part : TITLE_BAR_NONE, tab)) {
      invalidateTitleBar();
      updateTabTooltip();
    }
    return false;
  }
  case WM_MOUSELEAVE:
    m_trackingMouse = false;
    if (isClientButton(m_titleBar.hover()) && m_titleBar.pressed() == TITLE_BAR_NONE) {
      m_titleBar.setHover(TITLE_BAR_NONE);
      invalidateTitleBar();
      updateTabTooltip();
    }
    *result = 0;
    return true;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
  {
    size_t tab;
    TitleBarPart part = partAtClientPoint(lParam, &tab);
    if (!isClientButton(part)) {
      return false;
    }
    m_titleBar.setPressed(part, tab);
    m_titleBar.setHover(part, tab);
    SetCapture(m_hWnd);
    invalidateTitleBar();
    *result = 0;
    return true;
  }
  case WM_LBUTTONUP:
  {
    TitleBarPart pressed = m_titleBar.pressed();
    size_t pressedTab = m_titleBar.pressedTab();
    if (!isClientButton(pressed)) {
      return false;
    }
    m_titleBar.setPressed(TITLE_BAR_NONE);
    ReleaseCapture();
    invalidateTitleBar();
    size_t tab;
    if (partAtClientPoint(lParam, &tab) == pressed && tab == pressedTab) {
      executeTitleBarPart(pressed, tab);
    }
    *result = 0;
    return true;
  }
  case WM_MBUTTONDOWN:
  {
    // The middle button closes the tab it was pressed and released on.
    size_t tab;
    TitleBarPart part = partAtClientPoint(lParam, &tab);
    if (part != TITLE_BAR_TAB && part != TITLE_BAR_TAB_CLOSE) {
      return false;
    }
    m_middleTabId = m_tabs.at(tab) != 0 ? m_tabs.at(tab)->id() : 0;
    *result = 0;
    return true;
  }
  case WM_MBUTTONUP:
  {
    unsigned id = m_middleTabId;
    m_middleTabId = 0;
    size_t tab;
    TitleBarPart part = partAtClientPoint(lParam, &tab);
    if (id == 0 || (part != TITLE_BAR_TAB && part != TITLE_BAR_TAB_CLOSE)) {
      return false;
    }
    if (m_tabs.at(tab) != 0 && m_tabs.at(tab)->id() == id) {
      m_titleBar.setHover(TITLE_BAR_NONE);
      closeTab(tab);
    }
    *result = 0;
    return true;
  }
  case WM_CAPTURECHANGED:
    if (isClientButton(m_titleBar.pressed())) {
      m_titleBar.setPressed(TITLE_BAR_NONE);
      invalidateTitleBar();
    }
    return false;
  case WM_SETCURSOR:
    // The remote cursor is set on the window class, shared with the remote
    // desktop: the title bar keeps the arrow.
    if (reinterpret_cast<HWND>(wParam) == m_hWnd && LOWORD(lParam) == HTCLIENT) {
      SetCursor(LoadCursor(0, IDC_ARROW));
      *result = TRUE;
      return true;
    }
    return false;
  case WM_PAINT:
    paint();
    *result = 0;
    return true;
  case WM_ERASEBKGND:
    // WM_PAINT covers the bar and, without a session view, the content.
    *result = 1;
    return true;
  case WM_SIZE:
    m_chrome.resize(LOWORD(lParam), HIWORD(lParam));
    layoutTitleBar();
    return false;
  case WM_ACTIVATE:
    m_frameActive = LOWORD(wParam) != WA_INACTIVE;
    invalidateTitleBar();
    return false;
  case WM_DPICHANGED:
    m_chrome.setDpi(LOWORD(wParam));
    *result = callBaseProc(message, wParam, lParam);
    // A full screen frame keeps its size (no WM_SIZE): lay it out again.
    layoutTitleBar();
    layoutActiveTab();
    InvalidateRect(m_hWnd, 0, FALSE);
    return true;
  }
  return false;
}

int ShellWindow::titleBarHeight() const
{
  return m_isFullscreen ? 0 : m_chrome.dipsToPixels(m_titleBar.height());
}

int ShellWindow::maximizedFrameThickness() const
{
  UINT dpi = m_chrome.getDpi();
  return DpiUtils::getSystemMetrics(SM_CYFRAME, dpi) +
         DpiUtils::getSystemMetrics(SM_CXPADDEDBORDER, dpi);
}

LRESULT ShellWindow::hitTest(WPARAM wParam, LPARAM lParam)
{
  // The default hit test keeps the left, right and bottom resize borders.
  LRESULT hit = callBaseProc(WM_NCHITTEST, wParam, lParam);
  if (hit != HTCLIENT) {
    return hit;
  }
  POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
  ScreenToClient(m_hWnd, &point);
  if (point.y >= titleBarHeight()) {
    return HTCLIENT;
  }
  // Without a caption the top resize edge lies inside the title bar.
  if (!IsZoomed(m_hWnd)) {
    int edge = DpiUtils::scale(kTopResizeEdge, m_chrome.getDpi());
    if (point.y < edge) {
      RECT client;
      GetClientRect(m_hWnd, &client);
      int corner = 4 * edge;
      if (point.x < corner) {
        return HTTOPLEFT;
      }
      if (point.x >= client.right - corner) {
        return HTTOPRIGHT;
      }
      return HTTOP;
    }
  }
  switch (m_titleBar.hitTest(m_chrome.pixelsToDips(point.x), m_chrome.pixelsToDips(point.y))) {
  case TITLE_BAR_MINIMIZE:
    return HTMINBUTTON;
  case TITLE_BAR_MAXIMIZE:
    // Shows the Snap Layouts of Windows 11 on hover.
    return HTMAXBUTTON;
  case TITLE_BAR_CLOSE:
    return HTCLOSE;
  case TITLE_BAR_SETTINGS:
  case TITLE_BAR_NEW_TAB:
  case TITLE_BAR_TAB:
  case TITLE_BAR_TAB_CLOSE:
    return HTCLIENT;
  default:
    // Logo and empty space drag the window; a double click maximizes and a
    // right click opens the system menu (DefWindowProc).
    return HTCAPTION;
  }
}

TitleBarPart ShellWindow::windowButtonFromHitCode(WPARAM hitCode)
{
  switch (hitCode) {
  case HTMINBUTTON:
    return TITLE_BAR_MINIMIZE;
  case HTMAXBUTTON:
    return TITLE_BAR_MAXIMIZE;
  case HTCLOSE:
    return TITLE_BAR_CLOSE;
  default:
    return TITLE_BAR_NONE;
  }
}

TitleBarPart ShellWindow::partAtClientPoint(LPARAM lParam, size_t *tab) const
{
  if (tab != 0) {
    *tab = TitleBar::kNoTab;
  }
  if (m_isFullscreen) {
    return TITLE_BAR_NONE;
  }
  int x = static_cast<short>(LOWORD(lParam));
  int y = static_cast<short>(HIWORD(lParam));
  return m_titleBar.hitTest(m_chrome.pixelsToDips(x), m_chrome.pixelsToDips(y), tab);
}

void ShellWindow::trackMouseLeave(bool nonClient)
{
  bool &tracking = nonClient ? m_trackingNcMouse : m_trackingMouse;
  if (tracking) {
    return;
  }
  TRACKMOUSEEVENT track;
  track.cbSize = sizeof(track);
  track.dwFlags = TME_LEAVE | (nonClient ? TME_NONCLIENT : 0);
  track.hwndTrack = m_hWnd;
  track.dwHoverTime = 0;
  tracking = !!TrackMouseEvent(&track);
}

void ShellWindow::executeTitleBarPart(TitleBarPart part, size_t tab)
{
  m_titleBar.setHover(TITLE_BAR_NONE);
  switch (part) {
  case TITLE_BAR_MINIMIZE:
    SendMessage(m_hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    break;
  case TITLE_BAR_MAXIMIZE:
    SendMessage(m_hWnd, WM_SYSCOMMAND, IsZoomed(m_hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
    break;
  case TITLE_BAR_CLOSE:
    SendMessage(m_hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    break;
  case TITLE_BAR_SETTINGS:
    // Opens or closes the Settings page (onCommand()).
    PostMessage(m_hWnd, WM_COMMAND, IDS_TB_CONFIGURATION, 0);
    break;
  case TITLE_BAR_NEW_TAB:
    newTab();
    break;
  case TITLE_BAR_TAB:
    activateTab(tab);
    break;
  case TITLE_BAR_TAB_CLOSE:
    closeTab(tab);
    break;
  default:
    break;
  }
}

void ShellWindow::layoutTitleBar()
{
  RECT client;
  GetClientRect(m_hWnd, &client);
  m_titleBar.layout(m_chrome.pixelsToDips(client.right - client.left));
  updateTooltipRect();
  updateTabTooltip();
}

void ShellWindow::createTooltip()
{
  // The mouse messages of the window are relayed to it (onFrameMessage()).
  m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, 0,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              m_hWnd, 0, GetModuleHandle(0), 0);
  if (m_tooltip == 0) {
    return;
  }
  TOOLINFOW tool;
  ZeroMemory(&tool, sizeof(tool));
  tool.cbSize = sizeof(tool);
  tool.hwnd = m_hWnd;
  tool.uId = kNewTabToolId;
  tool.lpszText = const_cast<wchar_t *>(kNewTabTooltip);
  SendMessage(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
  // The tooltip of the tabs has no area until a tab with a cut title is
  // under the pointer (updateTabTooltip()).
  ZeroMemory(&tool, sizeof(tool));
  tool.cbSize = sizeof(tool);
  tool.hwnd = m_hWnd;
  tool.uId = kTabToolId;
  tool.lpszText = const_cast<wchar_t *>(L"");
  SendMessage(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

void ShellWindow::updateTabTooltip()
{
  if (m_tooltip == 0) {
    return;
  }
  TOOLINFOW tool;
  ZeroMemory(&tool, sizeof(tool));
  tool.cbSize = sizeof(tool);
  tool.hwnd = m_hWnd;
  tool.uId = kTabToolId;
  std::wstring text;
  size_t index = m_titleBar.hoverTab();
  if (index != TitleBar::kNoTab && index < m_titleBar.tabCount()) {
    const std::wstring &title = m_titleBar.tabTitle(index);
    float width = m_chrome.measureText(title.c_str(), currentTheme().fontSizeSmall);
    if (width > m_titleBar.tabLabelWidth(index)) {
      const D2D1_RECT_F &rect = m_titleBar.tabRect(index);
      tool.rect.left = m_chrome.dipsToPixels(rect.left);
      tool.rect.top = m_chrome.dipsToPixels(rect.top);
      tool.rect.right = m_chrome.dipsToPixels(rect.right);
      tool.rect.bottom = m_chrome.dipsToPixels(rect.bottom);
      text = title;
    }
  }
  SendMessage(m_tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
  tool.lpszText = const_cast<wchar_t *>(text.c_str());
  SendMessage(m_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void ShellWindow::updateTooltipRect()
{
  if (m_tooltip == 0) {
    return;
  }
  const D2D1_RECT_F &button = m_titleBar.newTabRect();
  TOOLINFOW tool;
  ZeroMemory(&tool, sizeof(tool));
  tool.cbSize = sizeof(tool);
  tool.hwnd = m_hWnd;
  tool.uId = kNewTabToolId;
  tool.rect.left = m_chrome.dipsToPixels(button.left);
  tool.rect.top = m_chrome.dipsToPixels(button.top);
  tool.rect.right = m_chrome.dipsToPixels(button.right);
  tool.rect.bottom = m_chrome.dipsToPixels(button.bottom);
  SendMessage(m_tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void ShellWindow::invalidateTitleBar()
{
  int height = titleBarHeight();
  if (height <= 0) {
    return;
  }
  RECT bar;
  GetClientRect(m_hWnd, &bar);
  bar.bottom = height;
  InvalidateRect(m_hWnd, &bar, FALSE);
}

void ShellWindow::paint()
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(m_hWnd, &ps);
  if (dc != 0 && !m_isFullscreen) {
    ShellTab *tab = visibleTab();
    bool hasView = tab != 0 && tab->viewHwnd() != 0;
    if (m_chrome.beginDraw()) {
      // The session view (WS_CLIPCHILDREN) covers the rest of the client
      // area; between two sessions of a reconnection it is empty.
      if (!hasView) {
        m_chrome.clear(currentTheme().background);
      }
      m_titleBar.paint(&m_chrome, m_frameActive, !!IsZoomed(m_hWnd));
      m_chrome.endDraw();
    } else {
      // No Direct2D: a plain bar.
      RECT area;
      GetClientRect(m_hWnd, &area);
      if (hasView) {
        area.bottom = titleBarHeight();
      }
      FillRect(dc, &area, GetSysColorBrush(COLOR_BTNFACE));
    }
  }
  EndPaint(m_hWnd, &ps);
}

void ShellWindow::updateDwmFrame()
{
  // One pixel of frame in the client area keeps the top border of Windows
  // 10 without a caption; full screen has no frame at all.
  DwmExtendFrameIntoClientAreaFunc extend = dwmExtendFrameIntoClientArea();
  if (extend != 0) {
    FrameMargins margins = { 0, 0, m_isFullscreen ? 0 : 1, 0 };
    extend(m_hWnd, &margins);
  }
}

SessionTab *ShellWindow::beginSessionTab(unsigned replaceId)
{
  // A tab that never got its session (its construction failed) goes away.
  m_staged.reset(new SessionTab(this));
  m_stagedReplaceId = replaceId;
  return m_staged.get();
}

void ShellWindow::attachSession(SessionTab *tab, ViewerInstance *instance)
{
  if (tab == 0 || m_staged.get() != tab) {
    return;
  }
  tab->bind(instance);
  unsigned replaceId = m_stagedReplaceId;
  m_stagedReplaceId = 0;
  insertTab(std::move(m_staged), replaceId);
}

size_t ShellWindow::indexOfId(unsigned id) const
{
  for (size_t i = 0; i < m_tabs.size(); i++) {
    if (m_tabs.at(i)->id() == id) {
      return i;
    }
  }
  return TabModel::npos;
}

void ShellWindow::insertTab(std::unique_ptr<ShellTab> tab, unsigned replaceId)
{
  if (tab->windowTitle().isEmpty()) {
    tab->setTitles(StringStorage(WindowNames::TVN_WINDOW_TITLE_NAME), tab->tabTitle());
  }
  size_t index = replaceId != 0 ? indexOfId(replaceId) : TabModel::npos;
  if (index == TabModel::npos) {
    // A new tab right after the active one, which is shown no more.
    ShellTab *old = m_tabs.active();
    // The Settings page goes away; the tab it covered stays hidden.
    dropSettings();
    if (old != 0) {
      // A full screen session leaves full screen here.
      old->onCovered();
      if (m_isFullscreen) {
        exitFullscreen();
      }
    }
    tab->setId(m_nextTabId++);
    m_tabs.add(std::move(tab));
    presentActive(old, true);
    return;
  }
  // The tab takes the place of another one (a session that ended to
  // reconnect, a Home page that connects) with its id and, for a session,
  // its titles.
  bool active = index == m_tabs.activeIndex();
  ShellTab *shown = tab.get();
  tab->setId(replaceId);
  if (!m_tabs.at(index)->isHome()) {
    tab->setTitles(m_tabs.at(index)->windowTitle(), m_tabs.at(index)->tabTitle());
  }
  if (active) {
    dropSettings();
  }
  // The Home page goes away with the tab that held it.
  m_tabs.replace(index, std::move(tab)).reset();
  if (active) {
    presentActive(0, true);
  } else {
    // Not the active tab: its session keeps running covered and hidden.
    if (shown->viewHwnd() != 0) {
      ShowWindow(shown->viewHwnd(), SW_HIDE);
    }
    shown->onCovered();
    refreshTabBar();
  }
}

void ShellWindow::presentActive(ShellTab *old, bool fresh)
{
  ShellTab *tab = m_tabs.active();
  if (tab == 0) {
    return;
  }
  m_lastFocus = 0;
  layoutActiveTab();
  if (tab->viewHwnd() != 0) {
    ShowWindow(tab->viewHwnd(), SW_SHOW);
  }
  if (!fresh) {
    tab->onUncovered();
  }
  // A reconnected session arrives in an active frame (no WM_ACTIVATE).
  if (GetActiveWindow() == m_hWnd) {
    focusActiveTab();
    // The keyboard hook of a session follows the activation of the frame.
    if (!fresh) {
      tab->onActivate(MAKEWPARAM(WA_ACTIVE, 0));
    }
  }
  if (old != 0 && old != tab && old->viewHwnd() != 0) {
    ShowWindow(old->viewHwnd(), SW_HIDE);
  }
  applyWindowTitle();
  refreshTabBar();
  InvalidateRect(m_hWnd, 0, FALSE);
}

void ShellWindow::removeTab(size_t index)
{
  ShellTab *tab = m_tabs.at(index);
  if (tab == 0) {
    return;
  }
  bool active = index == m_tabs.activeIndex();
  bool session = tab->asSession() != 0;
  HWND view = tab->viewHwnd();
  if (active) {
    // The page goes with the tab it covered; full screen ends first.
    dropSettings();
    if (m_isFullscreen) {
      exitFullscreen();
    }
    m_lastFocus = 0;
  }
  retireTab(m_tabs.remove(index));
  // The view of a session is destroyed here; a Home page went with its tab.
  if (session && view != 0) {
    DestroyWindow(view);
  }
  if (m_tabs.empty()) {
    destroyWindow();
    return;
  }
  if (active) {
    presentActive(0, false);
  } else {
    refreshTabBar();
  }
}

void ShellWindow::retireTab(std::unique_ptr<ShellTab> tab)
{
  SessionTab *session = tab ? tab->asSession() : 0;
  if (session != 0) {
    session->detach();
    m_retired.push_back(std::move(tab));
  }
}

void ShellWindow::retireAllTabs()
{
  while (!m_tabs.empty()) {
    retireTab(m_tabs.remove(m_tabs.size() - 1));
  }
}

bool ShellWindow::isCurrent(const ShellTab *tab) const
{
  return tab != 0 && m_tabs.active() == tab;
}

bool ShellWindow::isFrameOwner(const ShellTab *tab) const
{
  return m_tabs.indexOf(tab) != TabModel::npos &&
         (m_tabs.size() == 1 || !IsWindowVisible(m_hWnd));
}

void ShellWindow::showHome()
{
  if (m_hWnd == 0) {
    return;
  }
  std::unique_ptr<HomeTab> home(new HomeTab(m_hWnd, m_home));
  home->setTitles(StringStorage(WindowNames::TVN_WINDOW_TITLE_NAME), toWide(kHomeTitle));
  insertTab(std::move(home), 0);
  if (!IsWindowVisible(m_hWnd)) {
    // A new window, or one whose session ended before it was shown (a
    // connection from the command line that failed).
    requestClientSize(m_chrome.dipsToPixels(kHomeWidth), m_chrome.dipsToPixels(kHomeHeight));
    ShowWindow(m_hWnd, SW_SHOW);
    SetForegroundWindow(m_hWnd);
  }
  InvalidateRect(m_hWnd, 0, FALSE);
}

bool ShellWindow::isShowingHome() const
{
  ShellTab *tab = m_tabs.active();
  return tab != 0 && tab->isHome();
}

unsigned ShellWindow::activeTabId() const
{
  ShellTab *tab = m_tabs.active();
  return tab != 0 ? tab->id() : 0;
}

bool ShellWindow::hasTab(unsigned id) const
{
  return id != 0 && indexOfId(id) != TabModel::npos;
}

bool ShellWindow::hasHomeTab(unsigned id) const
{
  size_t index = id != 0 ? indexOfId(id) : TabModel::npos;
  return index != TabModel::npos && m_tabs.at(index)->isHome();
}

size_t ShellWindow::sessionCount() const
{
  size_t count = 0;
  for (size_t i = 0; i < m_tabs.size(); i++) {
    if (!m_tabs.at(i)->isHome()) {
      count++;
    }
  }
  return count;
}

void ShellWindow::onAppStateChanged()
{
  for (size_t i = 0; i < m_tabs.size(); i++) {
    m_tabs.at(i)->onAppStateChanged();
  }
}

ShellTab *ShellWindow::visibleTab() const
{
  if (m_settings) {
    return m_settings.get();
  }
  return m_tabs.active();
}

bool ShellWindow::isShowingSettings() const
{
  return !!m_settings;
}

void ShellWindow::newTab()
{
  showHome();
}

void ShellWindow::activateTab(size_t index)
{
  if (index >= m_tabs.size() || index == m_tabs.activeIndex()) {
    return;
  }
  // The Settings page covers the active tab only: it goes away, the tab it
  // covered stays hidden and covered.
  dropSettings();
  ShellTab *old = m_tabs.active();
  // A full screen session leaves full screen here, while it is the current
  // tab.
  old->onCovered();
  if (m_isFullscreen) {
    exitFullscreen();
  }
  m_tabs.activate(index);
  presentActive(old, false);
}

void ShellWindow::activateNextTab()
{
  if (m_tabs.size() > 1) {
    activateTab((m_tabs.activeIndex() + 1) % m_tabs.size());
  }
}

void ShellWindow::activatePreviousTab()
{
  if (m_tabs.size() > 1) {
    activateTab((m_tabs.activeIndex() + m_tabs.size() - 1) % m_tabs.size());
  }
}

void ShellWindow::closeTab(size_t index)
{
  ShellTab *tab = m_tabs.at(index);
  if (tab == 0) {
    return;
  }
  if (m_settings && index == m_tabs.activeIndex()) {
    // The tab of the bar reads "Settings": it closes the page.
    closeSettings();
    return;
  }
  if (tab->isClosing()) {
    return;
  }
  tab->setClosing(true);
  if (tab->requestClose()) {
    size_t now = m_tabs.indexOf(tab);
    if (now != TabModel::npos) {
      removeTab(now);
    }
    return;
  }
  // The session was asked to close: its end removes the tab
  // (frameSessionEnded()), unless it refuses (file transfer in progress).
  if (m_tabs.indexOf(tab) != TabModel::npos) {
    tab->setClosing(false);
  }
}

void ShellWindow::closeActiveTab()
{
  closeTab(m_tabs.activeIndex());
}

bool ShellWindow::onShortcutKey(WPARAM key)
{
  ShellTab *tab = visibleTab();
  if (tab == 0 || tab->asSession() != 0) {
    return false;
  }
  fsv::shell::TabShortcut shortcut = fsv::shell::tabShortcutFromKey(
    static_cast<unsigned>(key), GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0,
    GetKeyState(VK_MENU) < 0);
  switch (shortcut) {
  case fsv::shell::TabShortcut::NewTab:
    newTab();
    return true;
  case fsv::shell::TabShortcut::CloseTab:
    closeActiveTab();
    return true;
  case fsv::shell::TabShortcut::NextTab:
    activateNextTab();
    return true;
  case fsv::shell::TabShortcut::PreviousTab:
    activatePreviousTab();
    return true;
  default:
    return false;
  }
}

bool ShellWindow::canTakeIncomingTab() const
{
  return !m_destroyed && m_hWnd != 0 && IsWindowVisible(m_hWnd) && !m_isFullscreen &&
         !m_tabs.empty();
}

void ShellWindow::openSettings(SettingsSectionId section)
{
  if (m_hWnd == 0) {
    return;
  }
  s_lastSection = section;
  if (m_settings) {
    m_settings->showSection(section);
    return;
  }
  ShellTab *covered = m_tabs.active();
  m_settings.reset(new SettingsTab(m_hWnd, m_settingsActions, section));
  // The focus leaves the covered view before it is hidden: a hidden remote
  // desktop must never keep the keyboard (its keys would reach the server).
  m_lastFocus = 0;
  layoutActiveTab();
  if (GetActiveWindow() == m_hWnd) {
    focusActiveTab();
  }
  if (covered != 0) {
    covered->onCovered();
    if (covered->viewHwnd() != 0) {
      ShowWindow(covered->viewHwnd(), SW_HIDE);
    }
  }
  m_titleBar.setSettingsActive(true);
  updateTabTitle();
  InvalidateRect(m_hWnd, 0, FALSE);
}

void ShellWindow::closeSettings()
{
  if (!m_settings) {
    return;
  }
  s_lastSection = m_settings->section();
  ShellTab *covered = m_tabs.active();
  if (covered != 0 && covered->viewHwnd() != 0) {
    ShowWindow(covered->viewHwnd(), SW_SHOW);
  }
  m_lastFocus = 0;
  // The covered tab takes the focus before the page goes away.
  bool active = GetActiveWindow() == m_hWnd;
  if (active && covered != 0 && covered->focusHwnd() != 0) {
    SetFocus(covered->focusHwnd());
  }
  m_settings.reset();
  m_titleBar.setSettingsActive(false);
  updateTabTitle();
  layoutActiveTab();
  if (covered != 0) {
    covered->onUncovered();
    // The keyboard hook of a session follows the activation of the frame.
    if (active) {
      covered->onActivate(MAKEWPARAM(WA_ACTIVE, 0));
      focusActiveTab();
    }
  }
  InvalidateRect(m_hWnd, 0, FALSE);
}

void ShellWindow::dropSettings()
{
  if (!m_settings) {
    return;
  }
  s_lastSection = m_settings->section();
  m_settings.reset();
  m_lastFocus = 0;
  m_titleBar.setSettingsActive(false);
  updateTabTitle();
}

void ShellWindow::updateTabTitle()
{
  refreshTabBar();
}

void ShellWindow::refreshTabBar()
{
  std::vector<std::wstring> titles;
  for (size_t i = 0; i < m_tabs.size(); i++) {
    const ShellTab *tab = m_tabs.at(i);
    std::wstring title = tab->tabTitle();
    if (title.empty()) {
      title = toWide(tab->windowTitle().getString());
    }
    if (m_settings && i == m_tabs.activeIndex()) {
      title = kSettingsTitle;
    }
    titles.push_back(title);
  }
  m_titleBar.setTabs(titles, m_tabs.empty() ? 0 : m_tabs.activeIndex());
  updateTooltipRect();
  updateTabTooltip();
  invalidateTitleBar();
}

void ShellWindow::applyWindowTitle()
{
  // The window keeps the full title (taskbar, Alt+Tab) of the active tab.
  ShellTab *tab = m_tabs.active();
  if (tab != 0 && !tab->windowTitle().isEmpty()) {
    setWindowText(&tab->windowTitle());
  }
}

void ShellWindow::refreshSystemMenu()
{
  HMENU menu = GetSystemMenu(m_hWnd, FALSE);
  if (menu == 0) {
    return;
  }
  for (int i = GetMenuItemCount(menu) - 1; i >= 0; i--) {
    MENUITEMINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_DATA;
    if (GetMenuItemInfo(menu, i, TRUE, &info) && info.dwItemData == kSessionMenuTag) {
      DeleteMenu(menu, i, MF_BYPOSITION);
    }
  }
  ShellTab *tab = m_tabs.active();
  HMENU source = tab != 0 ? tab->sessionMenu() : 0;
  if (source == 0) {
    return;
  }
  // Before the separator that precedes Close.
  int position = GetMenuItemCount(menu);
  for (int i = 0; i < position; i++) {
    if (GetMenuItemID(menu, i) == SC_CLOSE) {
      position = i > 0 ? i - 1 : i;
      break;
    }
  }
  copyMenuItems(source, menu, position, true);
}

bool ShellWindow::isDestroyed() const
{
  return m_destroyed;
}

void ShellWindow::frameShow(const ShellTab *tab)
{
  if (isFrameOwner(tab)) {
    show();
  }
}

void ShellWindow::frameSetTitle(const ShellTab *tab, const StringStorage *windowTitle,
                                const StringStorage *tabTitle)
{
  size_t index = m_tabs.indexOf(tab);
  if (index == TabModel::npos) {
    return;
  }
  // The tab shows only the session name (the window title while there is
  // none); the window keeps the full title while the tab is the active one.
  const StringStorage *label = tabTitle->isEmpty() ? windowTitle : tabTitle;
  m_tabs.at(index)->setTitles(*windowTitle, toWide(label->getString()));
  if (index == m_tabs.activeIndex()) {
    applyWindowTitle();
  }
  refreshTabBar();
}

void ShellWindow::frameEnterFullscreen(const ShellTab *tab, const RECT &monitorRect)
{
  if (isCurrent(tab)) {
    enterFullscreen(monitorRect);
  }
}

void ShellWindow::frameExitFullscreen(const ShellTab *tab)
{
  if (isCurrent(tab)) {
    exitFullscreen();
  }
}

bool ShellWindow::frameIsFullscreen(const ShellTab *tab) const
{
  return isCurrent(tab) && m_isFullscreen;
}

void ShellWindow::frameRequestClientSize(const ShellTab *tab, int width, int height)
{
  if (isFrameOwner(tab)) {
    requestClientSize(width, height);
  }
}

void ShellWindow::frameOpenMaximized(const ShellTab *tab)
{
  if (isFrameOwner(tab)) {
    openMaximized();
  }
}

void ShellWindow::enterFullscreen(const RECT &monitorRect)
{
  // Preserve the windowed placement while changing fullscreen monitors.
  if (!m_isFullscreen) {
    GetWindowPlacement(m_hWnd, &m_windowedPlacement);
  }

  Rect fullScreenRect;
  fullScreenRect.fromWindowsRect(&monitorRect);
  m_logWriter.detail(_T("full screen window rect: %d, %d; %d, %d"),
                     fullScreenRect.left, fullScreenRect.top,
                     fullScreenRect.getWidth(), fullScreenRect.getHeight());

  setStyle((getStyle() | WS_MAXIMIZE) &
           ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME | WS_MAXIMIZEBOX));

  // Set first: the frame change below lays the window out without title bar.
  m_isFullscreen = true;
  updateDwmFrame();
  // Truly topmost, like AnyDesk: the frame stays above other windows when
  // it loses the activation (Alt+Tab). Setting WS_EX_TOPMOST in the style
  // bits alone does not move the window to the topmost band. Windows owned
  // by the shell (full screen toolbar, dialogs, message boxes) stay above
  // it.
  SetWindowPos(m_hWnd, HWND_TOPMOST,
               fullScreenRect.left, fullScreenRect.top,
               fullScreenRect.getWidth(), fullScreenRect.getHeight(),
               SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

void ShellWindow::exitFullscreen()
{
  if (!m_isFullscreen) {
    return;
  }
  m_isFullscreen = false;

  setStyle((getStyle() | WS_CAPTION | WS_BORDER | WS_THICKFRAME | WS_MAXIMIZEBOX) &
           ~WS_MAXIMIZE);
  updateDwmFrame();
  SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  // Back to the frame saved by enterFullscreen(), maximized or not. A frame
  // that keeps the default size follows a new remote screen size afterwards
  // (requestClientSize()).
  SetWindowPlacement(m_hWnd, &m_windowedPlacement);
}

bool ShellWindow::isFullscreen() const
{
  return m_isFullscreen;
}

Rect ShellWindow::calculateDefaultSize(int width, int height)
{
  // Get work area.
  Rect defaultRect;

  HMONITOR hmon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);

  if (!!GetMonitorInfo(hmon, &mi)) {
    defaultRect.fromWindowsRect(&mi.rcWork);
  } else {
    m_logWriter.debug(_T("Get monitor info is failed. Use second method (no multi-screen)."));
    RECT desktopRc;
    if (!m_sysinf.getDesktopArea(&desktopRc)) {
       m_sysinf.getDesktopAllArea(&desktopRc);
    }
    defaultRect.fromWindowsRect(&desktopRc);
  }

  int widthDesktop  = defaultRect.getWidth();
  int heightDesktop = defaultRect.getHeight();

  // The frame around the session: resize borders on the left, right and
  // bottom (WM_NCCALCSIZE removes the caption) and the title bar.
  int borderWidth, borderHeight;
  RECT frame = { 0, 0, 0, 0 };
  if (DpiUtils::adjustWindowRect(&frame, getStyle(), getExStyle(), m_chrome.getDpi())) {
    borderWidth = frame.right - frame.left;
    borderHeight = frame.bottom;
  } else {
    getBorderSize(&borderWidth, &borderHeight);
  }
  borderHeight += titleBarHeight();
  int totalWidth  = width + borderWidth;
  int totalHeight = height + borderHeight;
  // The whole window (frame and toolbar included) must fit in the work area,
  // otherwise tall desktops would be placed partly outside of the screen.
  if (totalWidth <= widthDesktop && totalHeight <= heightDesktop) {
    defaultRect.setHeight(totalHeight);
    defaultRect.setWidth(totalWidth);
    defaultRect.move((widthDesktop - totalWidth) / 2,
                     (heightDesktop - totalHeight) / 2);
  }
  return defaultRect;
}

void ShellWindow::requestClientSize(int width, int height)
{
  Rect defaultSize = calculateDefaultSize(width, height);
  bool defaultSizeIsChanged = defaultSize.getWidth() != m_rcNormal.getWidth() ||
                              defaultSize.getHeight() != m_rcNormal.getHeight();
  if (defaultSizeIsChanged) {
    m_rcNormal = defaultSize;
    setPosition(m_rcNormal.left, m_rcNormal.top);
    setSize(m_rcNormal.getWidth(), m_rcNormal.getHeight());
  }
}

void ShellWindow::openMaximized()
{
  // Like AnyDesk and RustDesk, a new frame opens maximized. A reconnected
  // session keeps the frame of the previous one.
  if (!m_sessionWasShown) {
    m_sessionWasShown = true;
    ShowWindow(m_hWnd, SW_MAXIMIZE);
  }
}

void ShellWindow::frameSessionEnded(const ShellTab *tab, bool reconnecting)
{
  size_t index = m_tabs.indexOf(tab);
  if (index == TabModel::npos) {
    return;
  }
  ShellTab *ended = m_tabs.at(index);
  if (!reconnecting && ended->isClosing()) {
    // The user closed the tab.
    removeTab(index);
    return;
  }
  bool active = index == m_tabs.activeIndex();
  HWND view = ended->viewHwnd();
  if (active) {
    // The page goes with the session it covered.
    dropSettings();
    m_lastFocus = 0;
  }
  if (reconnecting) {
    // The tab keeps its place and its titles until the next session is shown
    // in it (TvnViewer::runInstance()); the frame stays as it is, full
    // screen included.
    std::unique_ptr<ShellTab> pending(new ReconnectTab);
    pending->setId(ended->id());
    pending->setTitles(ended->windowTitle(), ended->tabTitle());
    retireTab(m_tabs.replace(index, std::move(pending)));
    if (view != 0) {
      DestroyWindow(view);
    }
    refreshTabBar();
    return;
  }
  // The session ended by itself (the server closed it, an error, the
  // reconnection was cancelled): the tab turns into a Home tab, in a
  // windowed frame.
  if (active && m_isFullscreen) {
    exitFullscreen();
  }
  std::unique_ptr<HomeTab> home(new HomeTab(m_hWnd, m_home));
  home->setId(ended->id());
  home->setTitles(StringStorage(WindowNames::TVN_WINDOW_TITLE_NAME), toWide(kHomeTitle));
  ShellTab *shown = home.get();
  retireTab(m_tabs.replace(index, std::move(home)));
  if (view != 0) {
    DestroyWindow(view);
  }
  if (active) {
    presentActive(0, true);
  } else {
    if (shown->viewHwnd() != 0) {
      ShowWindow(shown->viewHwnd(), SW_HIDE);
    }
    refreshTabBar();
  }
  if (!IsWindowVisible(m_hWnd)) {
    // A session of the command line that failed before it was shown.
    requestClientSize(m_chrome.dipsToPixels(kHomeWidth), m_chrome.dipsToPixels(kHomeHeight));
    ShowWindow(m_hWnd, SW_SHOW);
    SetForegroundWindow(m_hWnd);
  }
}

bool ShellWindow::onCommand(WPARAM wParam, LPARAM lParam)
{
  if (LOWORD(wParam) == IDS_TB_CONFIGURATION) {
    // The settings button (or a menu): the page, or back from it.
    if (m_settings) {
      closeSettings();
    } else {
      openSettings(s_lastSection);
    }
    return true;
  }
  if (m_settings) {
    // The keyboard belongs to the page: the session accelerators are off.
    if (HIWORD(wParam) == 1) {
      return true;
    }
    // Another command of the covered tab: the tab is shown for it.
    closeSettings();
  }
  ShellTab *tab = m_tabs.active();
  return tab != 0 && tab->onCommand(wParam, lParam);
}

bool ShellWindow::onSysCommand(WPARAM wParam, LPARAM lParam)
{
  // The session menu lives in the system menu of the shell. Commands the tab
  // does not know (move, size, close...) go to DefWindowProc.
  if ((wParam & 0xFFF0) < 0xF000 && (m_settings || LOWORD(wParam) == IDS_TB_CONFIGURATION)) {
    // The settings entry, or a session command of the system menu (the
    // session is shown for it).
    return onCommand(wParam, lParam);
  }
  ShellTab *tab = m_tabs.active();
  return tab != 0 && tab->onCommand(wParam, lParam);
}

bool ShellWindow::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
  case WM_SIZE:
    if (wParam != SIZE_MINIMIZED) {
      layoutActiveTab();
    }
    return true;
  case WM_SIZING:
    for (size_t i = 0; i < m_tabs.size(); i++) {
      m_tabs.at(i)->onFrameSizing();
    }
    if (m_settings) {
      m_settings->onFrameSizing();
    }
    return false;
  case WM_ACTIVATE:
    onActivate(wParam);
    return true;
  case WM_SETFOCUS:
    focusActiveTab();
    return true;
  case WM_CLOSE:
  {
    // Closes every tab, which ends their sessions; the last one takes the
    // window with it (removeTab()). A session may refuse to close (file
    // transfer in progress): the window stays with the tabs that remain.
    closeSettings();
    std::vector<ShellTab *> tabs;
    for (size_t i = 0; i < m_tabs.size(); i++) {
      tabs.push_back(m_tabs.at(i));
    }
    for (size_t i = tabs.size(); i > 0 && !m_destroyed; i--) {
      size_t index = m_tabs.indexOf(tabs[i - 1]);
      if (index != TabModel::npos) {
        closeTab(index);
      }
    }
    if (tabs.empty() && !m_destroyed) {
      destroyWindow();
    }
    return true;
  }
  case WM_DPICHANGED:
    onDpiChanged(wParam, lParam);
    return true;
  case WM_DISPLAYCHANGE:
    for (size_t i = 0; i < m_tabs.size(); i++) {
      m_tabs.at(i)->onDisplayChange();
    }
    return false;
  case WM_SETTINGCHANGE:
    // The user switched the Windows app theme: it only matters in Auto, and
    // refreshSystem() says whether the effective theme really changed.
    if (lParam != 0 &&
        wcscmp(reinterpret_cast<const wchar_t *>(lParam), L"ImmersiveColorSet") == 0 &&
        Theme::refreshSystem()) {
      Theme::broadcast();
    }
    return false;
  case WM_NCDESTROY:
    m_destroyed = true;
    m_settings.reset();
    retireAllTabs();
    // The handle is no longer valid (~BaseWindow must not destroy it).
    m_hWnd = 0;
    return false;
  }
  return false;
}

void ShellWindow::onActivate(WPARAM wParam)
{
  // A tab covered by the Settings page stays inactive (no keyboard hook).
  ShellTab *tab = visibleTab();
  bool active = LOWORD(wParam) != WA_INACTIVE;
  if (!active) {
    HWND focus = GetFocus();
    if (focus != 0 && IsChild(m_hWnd, focus)) {
      m_lastFocus = focus;
    }
  }
  if (tab != 0) {
    tab->onActivate(wParam);
  }
  // HIWORD is the minimized state: a minimized window keeps no focus.
  if (active && HIWORD(wParam) == 0) {
    focusActiveTab();
  }
}

void ShellWindow::onDpiChanged(WPARAM wParam, LPARAM lParam)
{
  const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
  // A windowed frame moved between monitors takes the rectangle suggested by
  // the system; a full screen frame keeps covering its monitor(s).
  bool frameMoved = !m_isFullscreen && suggested != 0;
  for (size_t i = 0; i < m_tabs.size(); i++) {
    m_tabs.at(i)->onDpiChanged(LOWORD(wParam), frameMoved);
  }
  if (m_settings) {
    m_settings->onDpiChanged(LOWORD(wParam), frameMoved);
  }
  if (frameMoved) {
    SetWindowPos(m_hWnd, 0, suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    // The default size follows the new DPI as well.
    m_rcNormal.fromWindowsRect(suggested);
  }
}

void ShellWindow::layoutActiveTab()
{
  // Below the title bar (none in full screen). A tab covered by the
  // Settings page keeps the size of the content area too.
  RECT rc;
  GetClientRect(m_hWnd, &rc);
  int top = titleBarHeight();
  int height = rc.bottom - rc.top - top;
  ShellTab *tabs[] = { m_tabs.active(), m_settings.get() };
  for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) {
    if (tabs[i] != 0 && tabs[i]->viewHwnd() != 0) {
      SetWindowPos(tabs[i]->viewHwnd(), 0, 0, top, rc.right - rc.left,
                   height > 0 ? height : 0, SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
}

void ShellWindow::focusActiveTab()
{
  // Never a window hidden under the Settings page.
  if (m_lastFocus != 0 && IsWindow(m_lastFocus) && IsChild(m_hWnd, m_lastFocus) &&
      IsWindowVisible(m_lastFocus)) {
    SetFocus(m_lastFocus);
    return;
  }
  ShellTab *tab = visibleTab();
  if (tab != 0 && tab->focusHwnd() != 0) {
    SetFocus(tab->focusHwnd());
  }
}
