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
// Modified for FarSightVNC on 2026-11-01: DPI changes, display modes and fullscreen monitor selection.
// Modified for FarSightVNC on 2026-11-03: floating auto-hide toolbar in full screen.
// Modified for FarSightVNC on 2026-11-03: connection state in the title and assisted reconnect.
// Modified for FarSightVNC on 2026-11-03: first connection opens maximized and fitted to the window.
// Modified for FarSightVNC on 2026-11-07: the connected session is shown by the window thread (WM_USER_CONNECTED).
// Modified for FarSightVNC on 2026-11-11: frame operations (title, full screen, size, DPI, end of session) go through SessionHost.
// Modified for FarSightVNC on 2026-11-11: child window (FsvSessionView) of the shell window, which forwards the frame events.
// Modified for FarSightVNC on 2026-11-11: the shell handles DPI changes and full screen state; a reconnection reuses the shell.
// Modified for FarSightVNC on 2026-11-11: the tab of the shell shows only the session name.
// Modified for FarSightVNC on 2026-11-11: SessionToolbar (Direct2D) replaces the comctl toolbar; the session state drives it.
// Modified for FarSightVNC on 2026-11-11: the full screen toolbar is a floating pill with the session actions and state.
//

#ifndef _VIEWER_WINDOW_H_
#define _VIEWER_WINDOW_H_

#include "ConnectionData.h"
#include "DesktopWindow.h"
#include "FileTransferMainDialog.h"
#include "FullScreenToolbar.h"
#include "NamingDefs.h"
#include "OptionsDialog.h"
#include "ScaleManager.h"
#include "ViewerMenu.h"
#include "shell/SessionHost.h"
#include "shell/SessionToolbar.h"
#include "log-writer/LogWriter.h"
#include "viewer-core/FileTransferCapability.h"
#include "viewer-core/RemoteViewerCore.h"
#include "viewer-core/CoreEventsAdapter.h"
#include "viewer-core/VncAuthentication.h"
#include "win-system/WindowsApplication.h"
#include "win-system/WinHooks.h"

class ViewerWindow : public BaseWindow,
                     public CoreEventsAdapter,
                     private HookEventListener,
                     private DesktopPointerListener,
                     private SessionToolbarListener
{
public:
  // Creates the session view as a child of host->hostHwnd().
  ViewerWindow(WindowsApplication *application,
               ConnectionData *conData, ConnectionConfig *conConf,
               SessionHost *host, Logger *logger = 0);
  virtual ~ViewerWindow();

  void setFileTransfer(FileTransferCapability *ft);
  void setRemoteViewerCore(RemoteViewerCore *pCore);

  //
  // This function return value of flag m_requiresReconnect.
  //
  bool requiresReconnect() const;

  //
  // This function return value of flag m_stopped.
  //
  bool isStopped() const;

  //
  // Shows the window before the session is established, with the
  // "connecting" state in its title. Used by reconnection attempts.
  //
  void showConnecting();

  //
  // Events of the top-level frame (the shell), which only a top-level
  // window receives.
  //
  // WM_COMMAND or WM_SYSCOMMAND; returns true when the command is handled.
  bool onFrameCommand(WPARAM wParam, LPARAM lParam);
  // WM_ACTIVATE: the keyboard hook follows the activation of the frame.
  void onFrameActivate(WPARAM wParam);
  // WM_SIZING: the user sizes the frame, which keeps that size from now on.
  void onFrameSizing();
  void onFrameDisplayChange();
  // WM_DPICHANGED; frameMoved when the windowed frame takes the rectangle
  // suggested by the system.
  void onFrameDpiChanged(UINT dpi, bool frameMoved);
  // Window that takes the keyboard focus (the remote desktop).
  HWND getFocusHwnd() const;
  // True when the frame is visible (a reconnection reuses a shown frame).
  bool isFrameShown() const;

  static const int WM_USER_ERROR = WM_USER + 1;
  static const int WM_USER_STOP = WM_USER + 2;
  static const int WM_USER_DISCONNECT = WM_USER + 3;
  static const int WM_USER_AUTH_ERROR = WM_USER + 4;
  static const int WM_USER_FS_WARNING = WM_USER + 5;
  static const int WM_USER_CONNECTED = WM_USER + 6;

protected:
  static const int TIMER_DESKTOP_STATE = 1;
  static const int TIMER_DESKTOP_STATE_DELAY = 50;

  bool onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  bool onEraseBackground(HDC hdc);
  
  bool onDisconnect();
  bool onAuthError(WPARAM wParam);
  bool onError();

  bool onFsWarning();
  // Shows the established session (window thread part of onConnected()).
  bool onSessionShown();
  // Offers to reconnect after an established session dropped
  // (sessionWasEstablished) or after a failed reconnection attempt. Returns
  // false, without any window, when reconnecting is not offered.
  bool offerReconnect(bool sessionWasEstablished, const StringStorage *reason);
  bool onSize(WPARAM wParam, LPARAM lParam);
  void onDisplayChange();
  bool onCreate(LPCREATESTRUCT lps);
  bool onCommand(WPARAM wParam, LPARAM lParam);
  bool onSysCommand(WPARAM wParam, LPARAM lParam);
  bool onClose();
  bool onDestroy();
  // Tears the session view down and tells the frame (reconnecting or not).
  void endSession();
  // Asks the application for a new session with conData and the current
  // options, shown in the same frame.
  void postReconnect(const ConnectionData &conData);
  bool onFocus(WPARAM wParam);
  bool onKillFocus(WPARAM wParam);
  bool onTimer(WPARAM idTimer);

  void desktopStateUpdate();
  void commandCtrlAltDel();
  void commandCtrlEsc();
  void commandCtrl();
  void commandAlt();
  void commandToolBar();
  void commandWinKey();
  void commandPause();
  void onAbout();
  void onCheckForUpdates();
  void commandNewConnection();
  void commandSaveSession();
  void commandScaleIn();
  void commandScaleOut();
  void commandScale100();
  void commandScaleAuto();

  //
  // It is implementation of CoreEventsAdapter functions.
  //
  void onBell();
  void onConnected(RfbOutputGate *output);
  void onDisconnect(const StringStorage *message);
  void onAuthError(const AuthException *exception);
  void onError(const Exception *exception);
  void onFrameBufferUpdate(const FrameBuffer *fb, const Rect *rect);
  void onFrameBufferPropChange(const FrameBuffer *fb);
  void onCutText(const StringStorage *cutText);

  int translateAccelToTB(int val);
  void applyScreenChanges(bool isFullScreen);
  

  LogWriter m_logWriter;

  Control m_control;

  ConnectionConfigSM m_ccsm;
  ConnectionConfig *m_conConf;
  WindowsApplication *m_application;
  RemoteViewerCore *m_viewerCore;
  FileTransferCapability *m_fileTransfer;
  FileTransferMainDialog *m_ftDialog;
  DesktopWindow m_dsktWnd;
  SessionToolbar m_toolbar;
  ViewerMenu m_menu;
  ConnectionData *m_conData;

  // The top-level frame (shell) that hosts the session view.
  SessionHost *m_host;

  // This variable save Exception after call onError().
  Exception m_error;
  // This variable save disconnect-message after call onDisconnect().
  StringStorage m_disconnectMessage;

  // Flag is set, if now viewer is in full screen mode
  bool m_isFullScr;


  // Flag is set after the first WM_SIZING of the frame.
  bool m_sizeIsChanged;
  // The first connection of the window opens it maximized and fitted.
  bool m_isFirstConnection;
  // The user wants the toolbar (hidden anyway in full screen).
  bool m_bToolBar;
  // Screen updates are paused (Pause command).
  bool m_isPaused;
  // It is scale of viewer window in percent.
  int m_scale;

  // Flag is set after onConnected().
  bool m_isConnected;

  // State shown in the title and in the full screen toolbar.
  enum ConnectionState
  {
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_DISCONNECTED
  };
  ConnectionState m_connectionState;
  void setConnectionState(ConnectionState state);
  const TCHAR *connectionStateLabel() const;
  // Text of the full screen pill: session name and connection state.
  StringStorage fullScreenToolbarLabel() const;

  // Flag is set, if instance is requires to reconnect.
  bool m_requiresReconnect;

  // Flag is set, if viewer instance is stopped.
  // Destructor of ViewerWindow may be called, if this flag is true.
  bool m_stopped;
private:
  vector<int> m_standardScale;
  vector<HMONITOR> m_fullscreenMonitors;
  HMONITOR m_fullscreenMonitor;
  bool m_spanMonitors;
  HMENU m_displayMenu;
  int m_fixedDisplayMenuItems;
  void addDisplayMenus();
  void refreshMonitorMenu();
  void applyFullscreenTarget();
  static BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data);
  void changeCursor(int type);
  void applySettings();
  void setSizeFullScreenWindow();
  void doFullScr();
  void doUnFullScr();
  void doSize();
  void doCommand(int iCommand);
  void showFileTransferDialog();
  void showWindow();
  void enableUserElements();
  bool viewerCoreSettings();
  void dialogConnectionOptions();
  void dialogConnectionInfo();
  void switchFullScreenMode();
  void dialogConfiguration();
  void adjustWindowSize();
  // Remote desktop name, or the host while it is unknown (tab title).
  StringStorage formatSessionName() const;
  StringStorage formatWindowName() const;
  // formatWindowName() with the connection state when it is not "connected".
  StringStorage formatWindowTitle() const;
  void updateKeyState();

  // The toolbar shows the session state: what can be used, which toggles
  // are on.
  SessionToolbarState toolbarState() const;
  void updateToolbar();
  // Implementation of SessionToolbarListener: the drop-down menus.
  virtual void onToolbarMenu(SessionAction action, const RECT &anchor);
  HMENU createMonitorMenu();
  HMENU createSpecialKeysMenu();
  HMENU createMoreMenu();

  // onHookProc function implementation of HookEventListener base abstract class.
  virtual LRESULT onHookProc(int code, WPARAM wParam, LPARAM lParam);
  WinHooks m_winHooks;
  bool m_hooksEnabledFirstTime;

  // Floating toolbar of the full screen mode; it exists only in full screen.
  FullScreenToolbar m_fsToolbar;
  HMONITOR fullScreenToolbarMonitor();
  // Implementation of DesktopPointerListener.
  virtual void onDesktopPointerMove(POINT clientPoint);
};

#endif
