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

#ifndef _SHELL_WINDOW_H_
#define _SHELL_WINDOW_H_

#include "HomeActions.h"
#include "SettingsActions.h"
#include "SettingsTab.h"
#include "SessionTab.h"
#include "ShellTab.h"
#include "TabModel.h"
#include "TitleBar.h"
#include "gui/d2d/ChromeRenderer.h"
#include "gui/BaseWindow.h"
#include "log-writer/LogWriter.h"
#include "region/Rect.h"
#include "win-system/SystemInformation.h"

#include <memory>
#include <vector>

class ViewerInstance;

// Tabs of a shell window and the active one.
typedef fsv::shell::TabModel<ShellTab> TabModel;

// Top-level frame of the viewer (window class TvnWindowClass): it hosts the
// views of its tabs (Home pages and sessions) as children below its title
// bar, shows the one of the active tab, owns the frame state (full screen,
// default size, DPI) and forwards the frame events to the tabs.
//
// A tab that is not the active one is hidden. Its session keeps running but
// is "covered" like by the Settings page (no focus, no keyboard hook, never
// full screen) and is uncovered when the tab is shown again. Only the active
// tab can be full screen; leaving it for another tab, or closing it, ends
// full screen first. The window title (taskbar) follows the active tab.
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
// deleted by the application (see isDestroyed()). A tab outlives its
// session: a reconnected session is shown in the same tab, and a session
// that ends by itself (closed by the server, disconnected, reconnection
// cancelled) turns its tab into a Home tab. A tab the user closes goes away
// once its session ended (a session may refuse during a file transfer) and
// the window is destroyed with the last one. Closing the window closes all
// its tabs; a tab that refuses keeps the window open.
class ShellWindow : public BaseWindow
{
public:
  // home receives the actions of the Home page.
  // settings receives the actions of the Settings page.
  ShellWindow(const TCHAR *windowClassName, Logger *logger, HomeActions *home,
              SettingsActions *settings);
  virtual ~ShellWindow();

  // Creates the tab of a session that is about to start, to be passed to its
  // ViewerInstance as the host, and then shown with attachSession(). With
  // replaceId the session takes the place of the tab with that id (the tab
  // of a session that ended to reconnect, or a Home tab); else it is a new
  // tab.
  SessionTab *beginSessionTab(unsigned replaceId = 0);
  // Shows the session of instance in tab, which beginSessionTab() returned.
  void attachSession(SessionTab *tab, ViewerInstance *instance);
  // Adds a Home tab and shows it; a frame that was never shown gets its
  // default Home size and is shown.
  void showHome();
  // The active tab is a Home page (the Settings page may cover it).
  bool isShowingHome() const;
  // Id of the active tab, 0 when there is none.
  unsigned activeTabId() const;
  // The shell has a tab with this id, or a Home tab with it.
  bool hasTab(unsigned id) const;
  bool hasHomeTab(unsigned id) const;
  // Tabs that are not a Home page: the sessions the shell keeps.
  size_t sessionCount() const;
  // The state of the application shown by the tabs changed.
  void onAppStateChanged();

  // True after the window received WM_NCDESTROY.
  bool isDestroyed() const;

  // The frame is full screen.
  bool isFullscreen() const;

  // Frame services of the session tabs: a SessionTab forwards the calls of
  // its SessionHost here, with itself as tab. Calls of a tab that is no
  // longer in the shell (an ended session) change nothing; the frame
  // services that concern the whole frame (full screen, size) only follow
  // the active tab, or the only tab.
  void frameShow(const ShellTab *tab);
  void frameSetTitle(const ShellTab *tab, const StringStorage *windowTitle,
                     const StringStorage *tabTitle);
  void frameEnterFullscreen(const ShellTab *tab, const RECT &monitorRect);
  void frameExitFullscreen(const ShellTab *tab);
  bool frameIsFullscreen(const ShellTab *tab) const;
  void frameRequestClientSize(const ShellTab *tab, int width, int height);
  void frameOpenMaximized(const ShellTab *tab);
  void frameSessionEnded(const ShellTab *tab, bool reconnecting);
  // Shows the Settings page of the shell on section, over the active tab.
  void openSettings(SettingsSectionId section);
  // Closes the Settings page and shows the tab it covers again.
  void closeSettings();
  bool isShowingSettings() const;

  // Tab commands (title bar, keyboard shortcuts). Each of them closes the
  // Settings page first when it is shown.
  void newTab();
  void activateTab(size_t index);
  void activateNextTab();
  void activatePreviousTab();
  // Closes the tab: a Home tab at once, a session through its window (it may
  // refuse). With the Settings page shown over the tab, closes the page.
  void closeTab(size_t index);
  void closeActiveTab();
  // Ctrl+T, Ctrl+W, Ctrl+Tab and Ctrl+Shift+Tab (ShellShortcuts.h) while the
  // Home page or the Settings page has the keyboard. In a session every key
  // goes to the remote computer: returns false then, and for any other key.
  // key is the virtual key of a WM_KEYDOWN.
  bool onShortcutKey(WPARAM key);
  // The shell can take an incoming connection as a new tab: it is shown and
  // not in full screen.
  bool canTakeIncomingTab() const;

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
  // Takes tab out of the shell. A session tab is kept (m_retired): its
  // session may still call it.
  void retireTab(std::unique_ptr<ShellTab> tab);
  void retireAllTabs();
  bool isCurrent(const ShellTab *tab) const;
  // The frame services of a tab are for the whole frame when the tab is the
  // active one, or the frame has no other tab, or was never shown.
  bool isFrameOwner(const ShellTab *tab) const;
  size_t indexOfId(unsigned id) const;
  // Adds tab after the active one and shows it, or puts it in the place of
  // the tab with replaceId (0: add). A new session is not "uncovered".
  void insertTab(std::unique_ptr<ShellTab> tab, unsigned replaceId);
  // The active tab changed from old (null: removed): shows the new one and
  // hides old. fresh: the tab is new, not coming back from the shadow.
  void presentActive(ShellTab *old, bool fresh);
  // Takes the tab at index out of the shell and destroys its view; the
  // window goes with the last tab.
  void removeTab(size_t index);
  // Applies the window title of the active tab.
  void applyWindowTitle();
  // Gives the title bar the titles of the tabs and the active one.
  void refreshTabBar();
  // Tooltip of the tab under the pointer when its title is cut.
  void updateTabTooltip();
  // Copies the session menu of the active tab into the system menu.
  void refreshSystemMenu();
  void enterFullscreen(const RECT &monitorRect);
  void exitFullscreen();
  void requestClientSize(int width, int height);
  void openMaximized();
  // The Settings page while it is shown, else the active tab.
  ShellTab *visibleTab() const;
  // Destroys the Settings page without showing the covered tab again (the
  // tab goes away too).
  void dropSettings();
  // The title of the active tab in the bar is "Settings" over the page.
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
  TitleBarPart partAtClientPoint(LPARAM lParam, size_t *tab = 0) const;
  void trackMouseLeave(bool nonClient);
  void executeTitleBarPart(TitleBarPart part, size_t tab);
  void focusActiveTab();
  // Frame rectangle for a client area of width x height on the work area of
  // the shell's monitor.
  Rect calculateDefaultSize(int width, int height);

  LogWriter m_logWriter;
  SystemInformation m_sysinf;
  HomeActions *m_home;
  SettingsActions *m_settingsActions;
  TabModel m_tabs;
  // Tab of the session that is being started, until attachSession(), and
  // the tab it replaces (0: none).
  std::unique_ptr<SessionTab> m_staged;
  unsigned m_stagedReplaceId;
  // Session tabs that left the shell.
  std::vector<std::unique_ptr<ShellTab> > m_retired;
  unsigned m_nextTabId;
  // The Settings page over the active tab, or none.
  std::unique_ptr<SettingsTab> m_settings;
  // Tab whose middle button went down, until it is released.
  unsigned m_middleTabId;
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
  // Tooltip of the "+" button and of the cut titles of the tabs (owned by
  // the window), or 0.
  HWND m_tooltip;
  ChromeRenderer m_chrome;
  TitleBar m_titleBar;
  bool m_frameActive;
  bool m_trackingMouse;
  bool m_trackingNcMouse;
};

#endif
