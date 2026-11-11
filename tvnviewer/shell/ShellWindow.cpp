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

#include "ShellWindow.h"

#include "gui/DpiUtils.h"
#include "../NamingDefs.h"
#include "../resource.h"

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

}

void TabModel::add(std::unique_ptr<ShellTab> tab)
{
  m_tabs.push_back(std::move(tab));
  m_active = m_tabs.size() - 1;
}

void TabModel::clear()
{
  m_tabs.clear();
  m_active = 0;
}

ShellTab *TabModel::active() const
{
  return m_active < m_tabs.size() ? m_tabs[m_active].get() : 0;
}

ShellWindow::ShellWindow(const TCHAR *windowClassName, Logger *logger)
: m_logWriter(logger),
  m_lastFocus(0),
  m_destroyed(false),
  m_sessionWasShown(false),
  m_isFullscreen(false),
  m_baseProc(0),
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
  updateDwmFrame();
  // Recalculates the frame without the caption (WM_NCCALCSIZE).
  SetWindowPos(m_hWnd, 0, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

ShellWindow::~ShellWindow()
{
  // The tabs reference sessions owned elsewhere: never call them from here.
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
      executeTitleBarPart(pressed);
    }
    *result = 0;
    return true;
  }
  case WM_MOUSEMOVE:
  {
    // Only the settings button of the bar is in the client area; the rest
    // of the client area belongs to the session view.
    TitleBarPart part = partAtClientPoint(lParam);
    if (part == TITLE_BAR_SETTINGS) {
      trackMouseLeave(false);
    }
    if (m_titleBar.setHover(part == TITLE_BAR_SETTINGS ? part : TITLE_BAR_NONE)) {
      invalidateTitleBar();
    }
    return false;
  }
  case WM_MOUSELEAVE:
    m_trackingMouse = false;
    if (m_titleBar.hover() == TITLE_BAR_SETTINGS && m_titleBar.pressed() == TITLE_BAR_NONE) {
      m_titleBar.setHover(TITLE_BAR_NONE);
      invalidateTitleBar();
    }
    *result = 0;
    return true;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
    if (partAtClientPoint(lParam) != TITLE_BAR_SETTINGS) {
      return false;
    }
    m_titleBar.setPressed(TITLE_BAR_SETTINGS);
    SetCapture(m_hWnd);
    invalidateTitleBar();
    *result = 0;
    return true;
  case WM_LBUTTONUP:
    if (m_titleBar.pressed() != TITLE_BAR_SETTINGS) {
      return false;
    }
    m_titleBar.setPressed(TITLE_BAR_NONE);
    ReleaseCapture();
    invalidateTitleBar();
    if (partAtClientPoint(lParam) == TITLE_BAR_SETTINGS) {
      executeTitleBarPart(TITLE_BAR_SETTINGS);
    }
    *result = 0;
    return true;
  case WM_CAPTURECHANGED:
    if (m_titleBar.pressed() == TITLE_BAR_SETTINGS) {
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
    return HTCLIENT;
  default:
    // Logo, tab and empty space drag the window; a double click maximizes
    // and a right click opens the system menu (DefWindowProc).
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

TitleBarPart ShellWindow::partAtClientPoint(LPARAM lParam) const
{
  if (m_isFullscreen) {
    return TITLE_BAR_NONE;
  }
  int x = static_cast<short>(LOWORD(lParam));
  int y = static_cast<short>(HIWORD(lParam));
  return m_titleBar.hitTest(m_chrome.pixelsToDips(x), m_chrome.pixelsToDips(y));
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

void ShellWindow::executeTitleBarPart(TitleBarPart part)
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
    // The configuration dialog, through the session (onCommand()).
    PostMessage(m_hWnd, WM_COMMAND, IDS_TB_CONFIGURATION, 0);
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
    ShellTab *tab = m_tabs.active();
    bool hasView = tab != 0 && tab->viewHwnd() != 0;
    if (m_chrome.beginDraw()) {
      // The session view (WS_CLIPCHILDREN) covers the rest of the client
      // area; between two sessions of a reconnection it is empty.
      if (!hasView) {
        m_chrome.clear(kLightTheme.background);
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

void ShellWindow::attachTab(std::unique_ptr<ShellTab> tab)
{
  m_tabs.add(std::move(tab));
  m_lastFocus = 0;
  layoutActiveTab();
  // A reconnected session arrives in an active frame (no WM_ACTIVATE).
  if (GetActiveWindow() == m_hWnd) {
    focusActiveTab();
  }
}

bool ShellWindow::isDestroyed() const
{
  return m_destroyed;
}

HWND ShellWindow::hostHwnd() const
{
  return m_hWnd;
}

HWND ShellWindow::rootHwnd() const
{
  return m_hWnd;
}

void ShellWindow::showFrame()
{
  show();
}

void ShellWindow::setTitle(const StringStorage *title)
{
  // The window keeps the title (taskbar, Alt+Tab); the tab shows it.
  setWindowText(title);
  m_titleBar.setTitle(toWide(title->getString()));
  invalidateTitleBar();
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

void ShellWindow::onSessionEnded(bool reconnecting)
{
  HWND view = 0;
  if (m_tabs.active() != 0) {
    view = m_tabs.active()->viewHwnd();
  }
  m_tabs.clear();
  m_lastFocus = 0;
  if (!reconnecting) {
    // The session view is a child: destroying the shell destroys it too. The
    // application deletes this object later (isDestroyed()).
    destroyWindow();
    return;
  }
  // The frame waits for the reconnected session (TvnViewer::runInstance()).
  if (view != 0) {
    DestroyWindow(view);
  }
  // The next session adds its own menu to a fresh system menu.
  GetSystemMenu(m_hWnd, TRUE);
}

bool ShellWindow::onCommand(WPARAM wParam, LPARAM lParam)
{
  ShellTab *tab = m_tabs.active();
  return tab != 0 && tab->onCommand(wParam, lParam);
}

bool ShellWindow::onSysCommand(WPARAM wParam, LPARAM lParam)
{
  // The session menu lives in the system menu of the shell. Commands the tab
  // does not know (move, size, close...) go to DefWindowProc.
  ShellTab *tab = m_tabs.active();
  return tab != 0 && tab->onCommand(wParam, lParam);
}

bool ShellWindow::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  ShellTab *tab = m_tabs.active();
  switch (message) {
  case WM_SIZE:
    if (wParam != SIZE_MINIMIZED) {
      layoutActiveTab();
    }
    return true;
  case WM_SIZING:
    if (tab != 0) {
      tab->onFrameSizing();
    }
    return false;
  case WM_ACTIVATE:
    onActivate(wParam);
    return true;
  case WM_SETFOCUS:
    focusActiveTab();
    return true;
  case WM_CLOSE:
    // Closing the shell stops the session; the session ends the shell (see
    // onSessionEnded()). Only the handle is kept: the tab goes away meanwhile.
    if (tab != 0) {
      HWND view = tab->viewHwnd();
      SendMessage(view, WM_CLOSE, 0, 0);
    } else {
      destroyWindow();
    }
    return true;
  case WM_DPICHANGED:
    onDpiChanged(wParam, lParam);
    return true;
  case WM_DISPLAYCHANGE:
    if (tab != 0) {
      tab->onDisplayChange();
    }
    return false;
  case WM_NCDESTROY:
    m_destroyed = true;
    m_tabs.clear();
    // The handle is no longer valid (~BaseWindow must not destroy it).
    m_hWnd = 0;
    return false;
  }
  return false;
}

void ShellWindow::onActivate(WPARAM wParam)
{
  ShellTab *tab = m_tabs.active();
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
  ShellTab *tab = m_tabs.active();
  if (tab != 0) {
    tab->onDpiChanged(LOWORD(wParam), frameMoved);
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
  ShellTab *tab = m_tabs.active();
  if (tab == 0 || tab->viewHwnd() == 0) {
    return;
  }
  // Below the title bar (none in full screen).
  RECT rc;
  GetClientRect(m_hWnd, &rc);
  int top = titleBarHeight();
  int height = rc.bottom - rc.top - top;
  SetWindowPos(tab->viewHwnd(), 0, 0, top, rc.right - rc.left, height > 0 ? height : 0,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void ShellWindow::focusActiveTab()
{
  if (m_lastFocus != 0 && IsWindow(m_lastFocus) && IsChild(m_hWnd, m_lastFocus)) {
    SetFocus(m_lastFocus);
    return;
  }
  ShellTab *tab = m_tabs.active();
  if (tab != 0 && tab->focusHwnd() != 0) {
    SetFocus(tab->focusHwnd());
  }
}
