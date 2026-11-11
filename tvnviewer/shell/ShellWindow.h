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

#ifndef _SHELL_WINDOW_H_
#define _SHELL_WINDOW_H_

#include "SessionHost.h"
#include "ShellTab.h"
#include "gui/BaseWindow.h"
#include "log-writer/LogWriter.h"
#include "region/Rect.h"
#include "win-system/SystemInformation.h"

#include <memory>
#include <vector>

// Tabs of a shell window and the active one. There is exactly one tab for
// now; the vector is the place of the browser-like tabs of a later phase.
class TabModel
{
public:
  TabModel() : m_active(0) {}

  void add(std::unique_ptr<ShellTab> tab);
  void clear();
  bool empty() const { return m_tabs.empty(); }
  // Active tab, or 0 when there is none.
  ShellTab *active() const;

private:
  std::vector<std::unique_ptr<ShellTab> > m_tabs;
  size_t m_active;
};

// Top-level frame of the viewer (window class TvnWindowClass): it hosts the
// session view as a child that fills its client area, owns the frame state
// (full screen, default size, DPI) and forwards the frame events to the
// active tab. Native caption for now.
//
// The application owns the shells; a shell whose window is destroyed is
// deleted by the application (see isDestroyed()). A shell outlives its
// session when the session is reconnected: the next session is shown in the
// same frame.
class ShellWindow : public BaseWindow,
                    public SessionHost
{
public:
  ShellWindow(const TCHAR *windowClassName, Logger *logger);
  virtual ~ShellWindow();

  // Shows tab in the content area and makes it the active tab.
  void attachTab(std::unique_ptr<ShellTab> tab);

  // True after the window received WM_NCDESTROY.
  bool isDestroyed() const;

  // SessionHost implementation.
  virtual HWND hostHwnd() const;
  virtual HWND rootHwnd() const;
  virtual void showFrame();
  virtual void setTitle(const StringStorage *title);
  virtual void enterFullscreen(const RECT &monitorRect);
  virtual void exitFullscreen();
  virtual bool isFullscreen() const;
  virtual void requestClientSize(int width, int height);
  virtual void openMaximized();
  virtual void onSessionEnded(bool reconnecting);

private:
  virtual bool onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  virtual bool onCommand(WPARAM wParam, LPARAM lParam);
  virtual bool onSysCommand(WPARAM wParam, LPARAM lParam);

  void onActivate(WPARAM wParam);
  void onDpiChanged(WPARAM wParam, LPARAM lParam);
  void layoutActiveTab();
  void focusActiveTab();
  // Frame rectangle for a client area of width x height on the work area of
  // the shell's monitor.
  Rect calculateDefaultSize(int width, int height);

  LogWriter m_logWriter;
  SystemInformation m_sysinf;
  TabModel m_tabs;
  // Focused descendant saved when the shell is deactivated.
  HWND m_lastFocus;
  bool m_destroyed;
  // A session was shown maximized or sized by the user in this frame: a
  // reconnected session keeps the frame as it is.
  bool m_sessionWasShown;

  bool m_isFullscreen;
  // Placement of the windowed frame (position, size, maximized state), saved
  // when entering full screen and restored when leaving it.
  WINDOWPLACEMENT m_windowedPlacement;
  // Default frame size applied by requestClientSize().
  Rect m_rcNormal;
};

#endif
