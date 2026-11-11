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
  m_isFullscreen(false)
{
  ZeroMemory(&m_windowedPlacement, sizeof(m_windowedPlacement));
  m_windowedPlacement.length = sizeof(m_windowedPlacement);

  StringStorage windowClass(windowClassName);
  StringStorage title(WindowNames::TVN_WINDOW_TITLE_NAME);
  setClass(&windowClass);
  createWindow(&title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
  loadIcon(IDI_APPICON);
}

ShellWindow::~ShellWindow()
{
  // The tabs reference sessions owned elsewhere: never call them from here.
  m_tabs.clear();
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
  setWindowText(title);
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
  setExStyle(getExStyle() | WS_EX_TOPMOST);

  SetWindowPos(m_hWnd, 0,
               fullScreenRect.left, fullScreenRect.top,
               fullScreenRect.getWidth(), fullScreenRect.getHeight(),
               SWP_SHOWWINDOW | SWP_FRAMECHANGED);
  m_isFullscreen = true;
}

void ShellWindow::exitFullscreen()
{
  if (!m_isFullscreen) {
    return;
  }
  m_isFullscreen = false;

  setStyle((getStyle() | WS_CAPTION | WS_BORDER | WS_THICKFRAME | WS_MAXIMIZEBOX) &
           ~WS_MAXIMIZE);
  setExStyle(getExStyle() & ~WS_EX_TOPMOST);
  SetWindowPos(m_hWnd, 0, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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

  int borderWidth, borderHeight;
  getBorderSize(&borderWidth, &borderHeight);
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
  case WM_ERASEBKGND:
    // The session view covers the whole client area.
    return tab != 0;
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
  RECT rc;
  GetClientRect(m_hWnd, &rc);
  SetWindowPos(tab->viewHwnd(), 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
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
