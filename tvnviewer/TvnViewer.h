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
//

#ifndef _TVN_VIEWER_H_
#define _TVN_VIEWER_H_

#include "AboutDialog.h"
#include "ConfigurationDialog.h"
#include "ConnectionListener.h"
#include "ControlTrayIcon.h"
#include "LoginDialog.h"
#include "OptionsDialog.h"
#include "ViewerCollector.h"
#include "shell/ShellWindow.h"
#include "win-system/WindowsApplication.h"

#include "log-writer/LogWriter.h"
#include "thread/AutoLock.h"

#include <list>
#include <map>
#include <memory>

class ViewerCollector;
class ControlTrayIcon;
class LoginDialog;
class UpdateController;

class TvnViewer : public WindowsApplication
{
public:
  TvnViewer(HINSTANCE appInstance,
            const TCHAR *windowClassName,
            const TCHAR *viewerWindowClassName);
  virtual ~TvnViewer();

  //
  // show login dialog
  //
  void showLoginDialog();

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

  // Inherited from WindowsApplication
  void registerWindowClass(WNDCLASS *wndClass);
  static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam);
  virtual void createWindow(const TCHAR *className);
  int processMessages();

public:
  // this message must sended after accepted new listening-connection
  static const int WM_USER_NEW_LISTENING = WM_USER + 1;

  // this message need send if you need show login dialog
  static const int WM_USER_SHOW_LOGIN_DIALOG = WM_USER + 2;

  // this message need send if you need show configuration dialog
  static const int WM_USER_CONFIGURATION = WM_USER + 3;

  // this message need send if you need show about dialog
  static const int WM_USER_ABOUT = WM_USER + 4;

  // This message need send if you need reconnect to host.
  // LPARAM is a ReconnectRequest allocated with new; the application
  // deletes it.
  static const int WM_USER_RECONNECT = WM_USER + 5;

  // Payload of WM_USER_RECONNECT.
  struct ReconnectRequest
  {
    ReconnectRequest(const ConnectionData &data, const ConnectionConfig &config, HWND frame)
    : conData(data), conConfig(config), shell(frame)
    {
    }
    ConnectionData conData;
    ConnectionConfig conConfig;
    // Shell window of the dropped session; the new session is shown in it
    // while it exists.
    HWND shell;
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

  // One-shot timer of the automatic update check (FarSightVNC).
  static const int TIMER_UPDATE_STARTUP = 2;
  static const int TIMER_UPDATE_STARTUP_DELAY = 5000;

protected:
  void startListeningServer(const int listeningPort);
  void stopListeningServer();
  void restartListeningServer();

  void registerViewerWindowClass();
  void unregisterViewerWindowClass();
  static LRESULT CALLBACK wndProcViewer(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam);

  // Starts a session in target, or in a new shell window when target is null.
  void runInstance(ConnectionData *conData, const ConnectionConfig *config,
                   ShellWindow *target = nullptr);
  void runInstance(const StringStorage *hostName, const ConnectionConfig *config);

  // Creates a hidden shell window, owned by the application.
  ShellWindow *createShell();
  // Shows the session of viewerInst in shell and starts it.
  void startInstance(ViewerInstance *viewerInst, ShellWindow *shell);
  // Deletes the shells whose window was destroyed.
  void deleteDeadShells();
  // Live shell with the window hwnd, or null.
  ShellWindow *findShell(HWND hwnd) const;

  // This method return true, if login dialog is visible.
  bool isVisibleLoginDialog() const;

  bool onTimer(WPARAM idTimer);

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

  AboutDialog m_aboutDialog;
  ConfigurationDialog m_configurationDialog;
  OptionsDialog m_optionsDialog;

  LoginDialog *m_loginDialog;
  ControlTrayIcon *m_trayIcon;
  ConnectionListener *m_conListener;
  std::unique_ptr<UpdateController> m_updateController;

  WNDCLASS m_viewerWndClass;
  WNDCLASS m_sessionViewWndClass;
};

#endif
