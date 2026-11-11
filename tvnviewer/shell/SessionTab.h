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

#ifndef _SESSION_TAB_H_
#define _SESSION_TAB_H_

#include "ShellTab.h"
#include "../ViewerInstance.h"

// Tab that shows a remote session. It references the ViewerInstance, owned by
// the ViewerCollector; the shell drops the tab when the session ends, before
// the collector deletes the instance.
class SessionTab : public ShellTab
{
public:
  explicit SessionTab(ViewerInstance *instance)
  : m_window(instance->getViewerWindow())
  {
  }

  virtual HWND viewHwnd() const { return m_window->getHWnd(); }
  virtual HWND focusHwnd() const { return m_window->getFocusHwnd(); }
  virtual bool onCommand(WPARAM wParam, LPARAM lParam) { return m_window->onFrameCommand(wParam, lParam); }
  virtual void onActivate(WPARAM wParam) { m_window->onFrameActivate(wParam); }
  virtual void onFrameSizing() { m_window->onFrameSizing(); }
  virtual void onDisplayChange() { m_window->onFrameDisplayChange(); }
  virtual void onDpiChanged(UINT dpi, bool frameMoved) { m_window->onFrameDpiChanged(dpi, frameMoved); }
  virtual bool requestClose()
  {
    // The session ends the shell (onSessionEnded()) unless it refuses to
    // close (file transfer in progress).
    SendMessage(m_window->getHWnd(), WM_CLOSE, 0, 0);
    return false;
  }

private:
  ViewerWindow *m_window;
};

#endif
