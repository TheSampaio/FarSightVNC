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
//

#ifndef _SHELL_WINDOW_H_
#define _SHELL_WINDOW_H_

#include "HomeActions.h"
#include "SessionHost.h"
#include "SettingsActions.h"
#include "SettingsTab.h"
#include "ShellTab.h"
#include "TitleBar.h"
#include "gui/d2d/ChromeRenderer.h"
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
// Home page or the session view as a child below its title bar, owns the
// frame state (full screen, default size, DPI) and forwards the frame events
// to the active tab.
//
// The title bar is drawn in the client area with Direct2D (TitleBar): the
// window keeps WS_OVERLAPPEDWINDOW (resize borders, Aero Snap, system menu),
// WM_NCCALCSIZE removes only the native caption and WM_NCHITTEST maps the
// bar to caption, window buttons (HTMAXBUTTON opens the Snap Layouts of
// Windows 11) and the top resize edge. In full screen there is no title bar.
//
// The settings button (IDS_TB_CONFIGURATION) shows the Settings page over the
// active tab, which the shell keeps: a session goes on running hidden, without
// the focus and the keyboard hook. Back, Esc or the button again closes the
// page and shows the tab as it was.
//
// The application owns the shells; a shell whose window is destroyed is
// deleted by the application (see isDestroyed()). A shell outlives its
// session: a reconnected session is shown in the same frame, and a session
// that ends (closed by the server, disconnected, reconnection cancelled)
// gives way to the Home page. Closing the shell ends its session and
// destroys the shell.
class ShellWindow : public BaseWindow,
                    public SessionHost
{
public:
  // home receives the actions of the Home page.
  // settings receives the actions of the Settings page.
  ShellWindow(const TCHAR *windowClassName, Logger *logger, HomeActions *home,
              SettingsActions *settings);
  virtual ~ShellWindow();

  // Shows tab in the content area and makes it the active tab. There is one
  // tab per shell until the browser-like tabs exist: tab replaces the
  // current one.
  void attachTab(std::unique_ptr<ShellTab> tab);
  // Shows the Home page in the content area; a frame that was never shown
  // gets its default Home size and is shown.
  void showHome();
  bool isShowingHome() const;
  // The state of the application shown by the tabs changed.
  void onAppStateChanged();

  // True after the window received WM_NCDESTROY.
  bool isDestroyed() const;

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

  // Closes the Settings page and shows the tab it covers again.
  void closeSettings();
  bool isShowingSettings() const;

private:
  // Window procedure installed over the one of the window class: the title
  // bar needs message results (hit test codes) that BaseWindow cannot
  // return. Unhandled messages go on to the class procedure.
  static LRESULT CALLBACK frameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  bool onFrameMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *result);
  LRESULT callBaseProc(UINT message, WPARAM wParam, LPARAM lParam);
  void restoreBaseProc();

  virtual bool onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  virtual bool onCommand(WPARAM wParam, LPARAM lParam);
  virtual bool onSysCommand(WPARAM wParam, LPARAM lParam);

  void onActivate(WPARAM wParam);
  void onDpiChanged(WPARAM wParam, LPARAM lParam);
  void layoutActiveTab();
  // The Settings page while it is shown, else the active tab.
  ShellTab *visibleTab() const;
  // Destroys the Settings page without showing the covered tab again (the
  // tab goes away too).
  void dropSettings();
  // Shows the tab title in the bar: "Settings" over the page.
  void updateTabTitle();

  // Title bar.
  LRESULT hitTest(WPARAM wParam, LPARAM lParam);
  // Height of the title bar in pixels, 0 in full screen.
  int titleBarHeight() const;
  // Resize frame of a maximized window that lies outside of the monitor.
  int maximizedFrameThickness() const;
  void layoutTitleBar();
  void createTooltip();
  void updateTooltipRect();
  void invalidateTitleBar();
  void paint();
  void updateDwmFrame();
  // Window button under a non-client hit test code, or TITLE_BAR_NONE.
  static TitleBarPart windowButtonFromHitCode(WPARAM hitCode);
  TitleBarPart partAtClientPoint(LPARAM lParam) const;
  void trackMouseLeave(bool nonClient);
  void executeTitleBarPart(TitleBarPart part);
  void focusActiveTab();
  // Frame rectangle for a client area of width x height on the work area of
  // the shell's monitor.
  Rect calculateDefaultSize(int width, int height);

  LogWriter m_logWriter;
  SystemInformation m_sysinf;
  HomeActions *m_home;
  SettingsActions *m_settingsActions;
  TabModel m_tabs;
  // The Settings page over the active tab, or none.
  std::unique_ptr<SettingsTab> m_settings;
  // Title of the active tab, kept while the page covers it.
  std::wstring m_tabTitle;
  // The active tab is the Home page.
  bool m_showsHome;
  // The user closes the shell: the end of the session destroys it instead
  // of showing the Home page.
  bool m_closeRequested;
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

  WNDPROC m_baseProc;
  // Tooltip of the "+" button (owned by the window), or 0.
  HWND m_tooltip;
  ChromeRenderer m_chrome;
  TitleBar m_titleBar;
  bool m_frameActive;
  bool m_trackingMouse;
  bool m_trackingNcMouse;
};

#endif
