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

#include "LegacyFrameHost.h"

LegacyFrameHost::LegacyFrameHost(BaseWindow *window, LogWriter *logWriter)
: m_window(window),
  m_logWriter(logWriter),
  m_isFullscreen(false)
{
  ZeroMemory(&m_workArea, sizeof(m_workArea));
  m_workArea.length = sizeof(m_workArea);
}

HWND LegacyFrameHost::hostHwnd() const
{
  return m_window->getHWnd();
}

HWND LegacyFrameHost::rootHwnd() const
{
  return m_window->getHWnd();
}

void LegacyFrameHost::showFrame()
{
  m_window->show();
}

void LegacyFrameHost::setTitle(const StringStorage *title)
{
  m_window->setWindowText(title);
}

void LegacyFrameHost::enterFullscreen(const RECT &monitorRect)
{
  // Preserve the windowed placement while changing fullscreen monitors.
  if (!m_isFullscreen) GetWindowPlacement(rootHwnd(), &m_workArea);

  Rect fullScreenRect;
  fullScreenRect.fromWindowsRect(&monitorRect);
  m_logWriter->detail(_T("full screen window rect: %d, %d; %d, %d"),
                      fullScreenRect.left, fullScreenRect.top,
                      fullScreenRect.getWidth(), fullScreenRect.getHeight());

  m_window->setStyle((m_window->getStyle() | WS_MAXIMIZE) &
                     ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME  | WS_MAXIMIZEBOX));
  m_window->setExStyle(m_window->getExStyle() | WS_EX_TOPMOST);

  SetWindowPos(rootHwnd(), 0,
               fullScreenRect.left, fullScreenRect.top,
               fullScreenRect.getWidth(), fullScreenRect.getHeight(),
               SWP_SHOWWINDOW);
  m_isFullscreen = true;
}

void LegacyFrameHost::exitFullscreen()
{
  m_isFullscreen = false;

  // Restore position, style and exstyle of windowed window.
  m_window->setStyle(m_window->getStyle() | WS_CAPTION | WS_BORDER | WS_THICKFRAME | WS_MAXIMIZEBOX);
  m_window->setExStyle(m_window->getExStyle() & ~WS_EX_TOPMOST);
  Rect workArea;
  workArea.fromWindowsRect(&m_workArea.rcNormalPosition);
  if (m_rcNormal.getHeight() == workArea.getHeight() ||
      m_rcNormal.getWidth() == workArea.getWidth()) {
    SetWindowPlacement(rootHwnd(), &m_workArea);
  } else {
    m_window->setStyle(m_window->getStyle() & ~WS_MAXIMIZE);
    m_window->setPosition(m_rcNormal.left, m_rcNormal.top);
    m_window->setSize(m_rcNormal.getWidth(), m_rcNormal.getHeight());
  }
}

bool LegacyFrameHost::isFullscreen() const
{
  return m_isFullscreen;
}

Rect LegacyFrameHost::calculateDefaultSize(int width, int height)
{
  // Get work area.
  Rect defaultRect;

  HMONITOR hmon = MonitorFromWindow(rootHwnd(), MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);

  if (!!GetMonitorInfo(hmon, &mi)) {
    defaultRect.fromWindowsRect(&mi.rcWork);
  } else {
    m_logWriter->debug(_T("Get monitor info is failed. Use second method (no multi-screen)."));
    RECT desktopRc;
    if (!m_sysinf.getDesktopArea(&desktopRc)) {
       m_sysinf.getDesktopAllArea(&desktopRc);
    }
    defaultRect.fromWindowsRect(&desktopRc);
  }

  int widthDesktop  = defaultRect.getWidth();
  int heightDesktop = defaultRect.getHeight();

  int borderWidth, borderHeight;
  m_window->getBorderSize(&borderWidth, &borderHeight);
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

void LegacyFrameHost::requestClientSize(int width, int height)
{
  Rect defaultSize = calculateDefaultSize(width, height);
  bool defaultSizeIsChanged = defaultSize.getWidth() != m_rcNormal.getWidth() ||
                              defaultSize.getHeight() != m_rcNormal.getHeight();
  if (defaultSizeIsChanged) {
    m_rcNormal = defaultSize;
    m_window->setPosition(m_rcNormal.left, m_rcNormal.top);
    m_window->setSize(m_rcNormal.getWidth(), m_rcNormal.getHeight());
  }
}

void LegacyFrameHost::maximize()
{
  ShowWindow(rootHwnd(), SW_MAXIMIZE);
}

void LegacyFrameHost::onDpiChanged(const RECT &suggested)
{
  SetWindowPos(rootHwnd(), 0, suggested.left, suggested.top,
               suggested.right - suggested.left,
               suggested.bottom - suggested.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
  // The size restored after fullscreen must follow the new DPI as well.
  m_rcNormal.fromWindowsRect(&suggested);
}

void LegacyFrameHost::onSessionEnded()
{
  m_window->destroyWindow();
}
