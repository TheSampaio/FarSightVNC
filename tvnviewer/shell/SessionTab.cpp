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

#include "SessionTab.h"

#include "ShellWindow.h"
#include "../ViewerInstance.h"

SessionTab::SessionTab(ShellWindow *shell)
: m_shell(shell),
  m_frame(shell->getHWnd()),
  m_window(0)
{
}

void SessionTab::bind(ViewerInstance *instance)
{
  m_window = instance->getViewerWindow();
}

void SessionTab::detach()
{
  m_shell = 0;
  m_window = 0;
}

HWND SessionTab::viewHwnd() const
{
  return m_window != 0 ? m_window->getHWnd() : 0;
}

HWND SessionTab::focusHwnd() const
{
  return m_window != 0 ? m_window->getFocusHwnd() : 0;
}

bool SessionTab::onCommand(WPARAM wParam, LPARAM lParam)
{
  return m_window != 0 && m_window->onFrameCommand(wParam, lParam);
}

void SessionTab::onActivate(WPARAM wParam)
{
  if (m_window != 0) {
    m_window->onFrameActivate(wParam);
  }
}

void SessionTab::onFrameSizing()
{
  if (m_window != 0) {
    m_window->onFrameSizing();
  }
}

void SessionTab::onDisplayChange()
{
  if (m_window != 0) {
    m_window->onFrameDisplayChange();
  }
}

void SessionTab::onDpiChanged(UINT dpi, bool frameMoved)
{
  if (m_window != 0) {
    m_window->onFrameDpiChanged(dpi, frameMoved);
  }
}

void SessionTab::onCovered()
{
  if (m_window != 0) {
    m_window->onFrameCovered();
  }
}

void SessionTab::onUncovered()
{
  if (m_window != 0) {
    m_window->onFrameUncovered();
  }
}

bool SessionTab::requestClose()
{
  if (m_window == 0) {
    // No session now (between two sessions of a reconnection).
    return true;
  }
  // The session ends the shell (onSessionEnded()) unless it refuses to
  // close (file transfer in progress).
  SendMessage(m_window->getHWnd(), WM_CLOSE, 0, 0);
  return false;
}

HWND SessionTab::hostHwnd() const
{
  return m_frame;
}

HWND SessionTab::rootHwnd() const
{
  return m_frame;
}

void SessionTab::showFrame()
{
  if (m_shell != 0) {
    m_shell->frameShow(this);
  }
}

void SessionTab::setTitle(const StringStorage *windowTitle, const StringStorage *tabTitle)
{
  if (m_shell != 0) {
    m_shell->frameSetTitle(this, windowTitle, tabTitle);
  }
}

void SessionTab::enterFullscreen(const RECT &monitorRect)
{
  if (m_shell != 0) {
    m_shell->frameEnterFullscreen(this, monitorRect);
  }
}

void SessionTab::exitFullscreen()
{
  if (m_shell != 0) {
    m_shell->frameExitFullscreen(this);
  }
}

bool SessionTab::isFullscreen() const
{
  return m_shell != 0 && m_shell->frameIsFullscreen(this);
}

void SessionTab::requestClientSize(int width, int height)
{
  if (m_shell != 0) {
    m_shell->frameRequestClientSize(this, width, height);
  }
}

void SessionTab::openMaximized()
{
  if (m_shell != 0) {
    m_shell->frameOpenMaximized(this);
  }
}

void SessionTab::onSessionEnded(bool reconnecting)
{
  // The shell may drop this tab; nothing here touches it afterwards.
  if (m_shell != 0) {
    m_shell->frameSessionEnded(this, reconnecting);
  }
}

void SessionTab::openSettings(SettingsSectionId section)
{
  if (m_shell != 0) {
    m_shell->openSettings(section);
  }
}
