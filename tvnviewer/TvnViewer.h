// Copyright (C) 2011,2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
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
//-------------------------------------------------------------------------
//
// Modified for FarSightVNC on 2026-11-11: every session is shown in its own shell window, owned by the application.
// Modified for FarSightVNC on 2026-11-11: a reconnection reuses the shell window of the session.
// Modified for FarSightVNC on 2026-11-11: the Home page of a shell window replaces the login dialog.
// Modified for FarSightVNC on 2026-11-11: the Settings page of a shell window (SettingsActions).
// Modified for FarSightVNC on 2026-11-11: the Settings page replaces the configuration and about dialogs.
// Modified for FarSightVNC on 2026-11-11: the messages of the removed configuration and about dialogs are gone.
// Modified for FarSightVNC on 2026-11-14: the automatic update check timer repeats; update notifications are owned by a shell and wait for full screen to end.
// Modified for FarSightVNC on 2026-11-14: "Restart and Install Now" closes every shell window to install a staged update.
// Modified for FarSightVNC on 2026-11-14: a CBT hook re-owns the update dialog before its owner shell is destroyed.
// Modified for FarSightVNC on 2026-11-22: every session has its own tab as host (SessionTab), created before the ViewerInstance.
// Modified for FarSightVNC on 2026-11-23: a reconnection or a connection from Home names the tab that shows the session.
// Modified for FarSightVNC on 2026-11-23: tab shortcuts on Home and Settings; an incoming connection is a tab of the most recently active shell.
//

#ifndef _TVN_VIEWER_H_
#define _TVN_VIEWER_H_

#include "ConnectionListener.h"
#include "ControlTrayIcon.h"
#include "OptionsDialog.h"
#include "ViewerCollector.h"
#include "shell/HomeActions.h"
#include "shell/ShellWindow.h"
#include "win-system/WindowsApplication.h"

#include "log-writer/LogWriter.h"
#include "thread/AutoLock.h"

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ViewerCollector;
class ControlTrayIcon;
class UpdateController;

class TvnViewer : public WindowsApplication,
                  public HomeActions,
                  public SettingsActions,
                  public UpdateController::Host
{
public:
  TvnViewer(HINSTANCE appInstance,
            const TCHAR *windowClassName,
            const TCHAR *viewerWindowClassName);
  virtual ~TvnViewer();

  // Opens a new shell window on the Home page (the start screen).
  void showHome();

  //
  // show options for listening mode
  //
  void showListeningOptions();

  //
  // show dialog "About of Viewer"
  //

  void showAboutViewer();

  //
  // show dialog with configuration of viewer
  //
  void showConfiguration();

  // newConnection(...) and startListening(...) always do copy of params (StringStorage,
  // ConnectionData and ConnectionConfig). After call this function can free memory
  // with hostName, connectionData, connectionConfig
  void newListeningConnection();
  void newConnection(const StringStorage *hostName, const ConnectionConfig *connectionConfig);
  void newConnection(const ConnectionData *conData, const ConnectionConfig *connectionConfig);
  void startListening(int listeningPort);
  void stopListening();

  // HomeActions implementation.
  virtual void connectFromHome(HWND shell, const StringStorage *host,
                               const ConnectionConfig *config);
  virtual bool isListening() const;
  virtual void toggleListening();
  virtual void openHome();

  // SettingsActions implementation.
  virtual void addDialogWindow(HWND window);
  virtual void removeDialogWindow(HWND window);
  virtual void onViewerConfigChanged();
  virtual UpdateController::Preferences updatePreferences();
  virtual void setUpdateAutoCheck(bool enabled);
  virtual void setUpdatePrereleases(bool enabled);
  virtual void checkForUpdates();

  // UpdateController::Host implementation.
  virtual std::vector<UpdateController::Host::Shell> updateShells();
  virtual void updateLog(const std::wstring &line);
  virtual int updateOpenSessions();
  virtual void updateCloseAllWindows();

  // Shows the Settings page on section in the foreground shell window, or
  // in a new one on the Home page (tray menu).
  void showSettings(SettingsSectionId section);

  // Inherited from WindowsApplication
  void registerWindowClass(WNDCLASS *wndClass);
  static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam);
  virtual void createWindow(const TCHAR *className);
  int processMessages();

public:
  // this message must sended after accepted new listening-connection
  static const int WM_USER_NEW_LISTENING = WM_USER + 1;

  // Opens a new shell window on the Home page (new connection).
  static const int WM_USER_SHOW_HOME = WM_USER + 2;

  // This message need send if you need reconnect to host.
  // LPARAM is a ReconnectRequest allocated with new; the application
  // deletes it.
  static const int WM_USER_RECONNECT = WM_USER + 5;

  // Connects from the Home page of a shell window, which then shows the
  // session. LPARAM is a ReconnectRequest allocated with new; the
  // application deletes it.
  static const int WM_USER_HOME_CONNECT = WM_USER + 11;

  // Payload of WM_USER_RECONNECT and WM_USER_HOME_CONNECT.
  struct ReconnectRequest
  {
    ReconnectRequest(const ConnectionData &data, const ConnectionConfig &config, HWND frame,
                     unsigned tab = 0)
    : conData(data), conConfig(config), shell(frame), tabId(tab)
    {
    }
    ConnectionData conData;
    ConnectionConfig conConfig;
    // Shell window of the dropped session (or of the Home page); the new
    // session is shown in it while it exists.
    HWND shell;
    // Tab of the shell that shows the new session in its place (the tab of
    // the dropped session, or the Home tab), 0: a new tab.
    unsigned tabId;
  };

  // This message need send if config is changed.
  static const int WM_USER_CONFIGURATION_RELOAD = WM_USER + 6;

  // This message need send if you need to check for updates (FarSightVNC).
  // It is posted from any thread of the application.
  static const int WM_USER_CHECK_UPDATES = WM_USER + 10;

  // Posted by the update worker thread: an update check finished.
  static const int WM_USER_UPDATE_CHECK_DONE = WM_USER + 7;

  // Posted by the update worker thread: download progress changed (coalesced).
  static const int WM_USER_UPDATE_PROGRESS = WM_USER + 8;

  // Posted by the update worker thread: downloading/staging an update finished.
  static const int WM_USER_UPDATE_STAGE_DONE = WM_USER + 9;

  // This timer is used for deleting dead instances of viewer.
  static const int TIMER_DELETE_DEAD_INSTANCE = 1;
  static const int TIMER_DELETE_DEAD_INSTANCE_DELAY = 50;

  // Timer of the automatic update check (FarSightVNC): the first tick comes
  // a few seconds after start, then one every TIMER_UPDATE_PERIOD; the update
  // policy decides whether a check is due.
  static const int TIMER_UPDATE_STARTUP = 2;
  static const int TIMER_UPDATE_STARTUP_DELAY = 5000;
  static const int TIMER_UPDATE_PERIOD = 30 * 60 * 1000;

  // While an automatic update notification waits for full screen to end.
  static const int TIMER_UPDATE_DEFERRED = 3;
  static const int TIMER_UPDATE_DEFERRED_DELAY = 2000;

protected:
  void startListeningServer(const int listeningPort);
  void stopListeningServer();
  void restartListeningServer();

  void registerViewerWindowClass();
  void unregisterViewerWindowClass();
  static LRESULT CALLBACK wndProcViewer(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam);

  // Starts a session in target, or in a new shell window when target is
  // null. replaceTab: the tab of target that shows it (0: a new tab).
  void runInstance(ConnectionData *conData, const ConnectionConfig *config,
                   ShellWindow *target = nullptr, unsigned replaceTab = 0);
  void runInstance(const StringStorage *hostName, const ConnectionConfig *config);

  // Creates a hidden shell window, owned by the application.
  ShellWindow *createShell();
  // Shows the session of viewerInst in tab of shell and starts it.
  void startInstance(ViewerInstance *viewerInst, ShellWindow *shell, SessionTab *tab);
  // Deletes the shells whose window was destroyed.
  void deleteDeadShells();
  // Tells the live shells that the listening mode changed.
  void notifyShells();
  // Live shell with the window hwnd, or null.
  ShellWindow *findShell(HWND hwnd) const;
  // Most recently active shell that can take an incoming connection as a new
  // tab (top of the z-order), or null.
  ShellWindow *findIncomingShell() const;
  // Gives a tab shortcut of the Home or Settings page (Ctrl+T...) to its
  // shell. True when the key was handled.
  bool handleTabShortcut(const MSG &msg);

  bool onTimer(WPARAM idTimer);

  // Thread WH_CBT hook: tells the update controller that a shell window is
  // about to be destroyed, before Windows destroys the windows it owns.
  static LRESULT CALLBACK cbtHookProc(int code, WPARAM wParam, LPARAM lParam);
  static TvnViewer *s_hookOwner;
  HHOOK m_cbtHook;

  ViewerCollector m_instances;
  // Top-level windows of the sessions. A shell is deleted after its window
  // is destroyed (deleteDeadShells()).
  std::list<std::unique_ptr<ShellWindow> > m_shells;

  // class name of viewer-window
  StringStorage m_viewerWindowClassName;

  HACCEL m_hAccelTable;

private:
  void addInstance(ViewerInstance *viewerInstance);

  bool m_isListening;
  
  LogWriter m_logWriter;

  OptionsDialog m_optionsDialog;

  ControlTrayIcon *m_trayIcon;
  ConnectionListener *m_conListener;
  std::unique_ptr<UpdateController> m_updateController;

  WNDCLASS m_viewerWndClass;
  WNDCLASS m_sessionViewWndClass;
};

#endif
