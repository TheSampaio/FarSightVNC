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
// Modified for FarSightVNC on 2026-11-22: the tab is the SessionHost of its session (created before the ViewerInstance, bound to it later).
//

#ifndef _SESSION_TAB_H_
#define _SESSION_TAB_H_

#include "SessionHost.h"
#include "ShellTab.h"

class ShellWindow;
class ViewerInstance;
class ViewerWindow;

// Tab that shows a remote session, and the SessionHost of that session: the
// session talks to its frame through the tab, which forwards the calls to the
// shell with its own identity, so that the shell knows which tab asks.
//
// The shell creates the tab before the session exists (the session window is
// created in the constructor of the ViewerInstance and needs its host), then
// binds it to the ViewerInstance, owned by the ViewerCollector. The shell
// drops the tab when the session ends, before the collector deletes the
// instance; a dropped tab stays alive (the shell keeps it) because the ended
// session may still call it, and it ignores those calls.
class SessionTab : public ShellTab,
                   public SessionHost
{
public:
  explicit SessionTab(ShellWindow *shell);

  // The tab shows the session of instance.
  void bind(ViewerInstance *instance);
  // The shell dropped the tab: the calls of the session end here.
  void detach();

  // ShellTab implementation.
  virtual HWND viewHwnd() const;
  virtual HWND focusHwnd() const;
  virtual bool onCommand(WPARAM wParam, LPARAM lParam);
  virtual void onActivate(WPARAM wParam);
  virtual void onFrameSizing();
  virtual void onDisplayChange();
  virtual void onDpiChanged(UINT dpi, bool frameMoved);
  virtual void onCovered();
  virtual void onUncovered();
  virtual bool requestClose();
  virtual SessionTab *asSession() { return this; }
  virtual HMENU sessionMenu() const;

  // SessionHost implementation.
  virtual HWND hostHwnd() const;
  virtual HWND rootHwnd() const;
  virtual void showFrame();
  virtual void setTitle(const StringStorage *windowTitle, const StringStorage *tabTitle);
  virtual void enterFullscreen(const RECT &monitorRect);
  virtual void exitFullscreen();
  virtual bool isFullscreen() const;
  virtual void requestClientSize(int width, int height);
  virtual void openMaximized();
  virtual void onSessionEnded(bool reconnecting);
  virtual void openSettings(SettingsSectionId section);
  virtual unsigned tabId() const;

private:
  // Null once the shell dropped the tab.
  ShellWindow *m_shell;
  // The shell window, kept for the session after the shell dropped the tab.
  HWND m_frame;
  // Null until bind().
  ViewerWindow *m_window;
};

#endif
