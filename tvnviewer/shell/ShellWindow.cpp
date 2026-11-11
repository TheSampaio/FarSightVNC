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
  m_frame(this, &m_logWriter),
  m_lastFocus(0),
  m_destroyed(false)
{
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
  m_frame.enterFullscreen(monitorRect);
}

void ShellWindow::exitFullscreen()
{
  m_frame.exitFullscreen();
}

bool ShellWindow::isFullscreen() const
{
  return m_frame.isFullscreen();
}

void ShellWindow::requestClientSize(int width, int height)
{
  m_frame.requestClientSize(width, height);
}

void ShellWindow::maximize()
{
  m_frame.maximize();
}

void ShellWindow::onDpiChanged(const RECT &suggested)
{
  m_frame.onDpiChanged(suggested);
}

void ShellWindow::onSessionEnded()
{
  // The session view is a child: destroying the shell destroys it too. The
  // application deletes this object later (isDestroyed()).
  m_tabs.clear();
  destroyWindow();
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
    if (tab != 0) {
      tab->onDpiChanged(wParam, lParam);
    } else if (lParam != 0) {
      m_frame.onDpiChanged(*reinterpret_cast<const RECT *>(lParam));
    }
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
