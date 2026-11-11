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
// Modified for FarSightVNC on 2026-11-01: ordered keyboard input, DPI changes, display menu, fullscreen monitor selection, default window size within the work area.
// Modified for FarSightVNC on 2026-11-03: floating auto-hide toolbar in full screen.
// Modified for FarSightVNC on 2026-11-03: connection state in the title and assisted reconnect.
// Modified for FarSightVNC on 2026-11-03: "FarSightVNC Viewer - <host>" title; sessions open maximized and fitted to the window.
// Modified for FarSightVNC on 2026-11-04: "FarSightVNC | <host>" title; system shortcuts go to the server in windowed mode too.
// Modified for FarSightVNC on 2026-11-07: a session opened in full screen no longer freezes the viewer (window work moved to the window thread).
// Modified for FarSightVNC on 2026-11-11: frame operations (title, full screen, size, DPI, end of session) go through SessionHost.
// Modified for FarSightVNC on 2026-11-11: child window (FsvSessionView) of the shell window; frame events come from the shell, dialogs are owned by it.
// Modified for FarSightVNC on 2026-11-11: the shell handles DPI changes and full screen state; a reconnection reuses the shell.
// Modified for FarSightVNC on 2026-11-11: the tab of the shell shows only the session name.
// Modified for FarSightVNC on 2026-11-11: SessionToolbar (Direct2D) replaces the comctl toolbar; the session state drives it.
// Modified for FarSightVNC on 2026-11-11: the full screen toolbar is a floating pill with the session actions and state.
// Modified for FarSightVNC on 2026-11-11: the remote screen always fits the window (no scale modes, no scroll bars).
// Modified for FarSightVNC on 2026-11-11: New connection opens a shell window on the Home page.
//

#include "config-lib/IniFileSettingsManager.h"
#include "gui/DpiUtils.h"
#include "util/Exception.h"
#include "util/ResourceLoader.h"
#include "rfb/StandardPixelFormatFactory.h"

#include "FsWarningDialog.h"
#include "NamingDefs.h"
#include "ReconnectDialog.h"
#include "ReconnectPolicy.h"
#include "TvnViewer.h"
#include "ViewerWindow.h"

ViewerWindow::ViewerWindow(WindowsApplication *application,
                           ConnectionData *conData,
                           ConnectionConfig *conConf,
                           SessionHost *host,
                           Logger *logger)
: m_ccsm(RegistryPaths::VIEWER_PATH,
         conData->getHost().getString()),
  m_application(application),
  m_logWriter(logger),
  m_conConf(conConf),
  m_isFullScr(false),
  m_ftDialog(0),
  m_viewerCore(0),
  m_fileTransfer(0),
  m_conData(conData),
  m_host(host),
  m_dsktWnd(&m_logWriter, conConf),
  m_isConnected(false),
  m_connectionState(STATE_CONNECTING),
  m_sizeIsChanged(false),
  m_isFirstConnection(true),
  m_bToolBar(true),
  m_isPaused(false),
  m_hooksEnabledFirstTime(true),
  m_requiresReconnect(false),
  m_stopped(false),
  m_fullscreenMonitor(0),
  m_spanMonitors(false),
  m_displayMenu(0),
  m_fixedDisplayMenuItems(0)
{
  StringStorage windowClass = WindowNames::TVN_WINDOW_CLASS_NAME;
  StringStorage viewClass = WindowNames::FSV_SESSION_VIEW_CLASS_NAME;
  StringStorage titleName = WindowNames::TVN_WINDOW_TITLE_NAME;
  StringStorage subTitleName = WindowNames::TVN_SUB_WINDOW_TITLE_NAME;

  // The session view fills the client area of the shell (the frame).
  setClass(&viewClass);
  createWindow(&titleName, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
               m_host->hostHwnd(), 0, 0, 0, 0);

  m_dsktWnd.setClass(&windowClass);
  m_dsktWnd.createWindow(&subTitleName,
                         WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CHILD,
                         getHWnd());
  m_dsktWnd.setPointerListener(this);
  // Clicks on the toolbar give the focus back to the remote desktop.
  m_toolbar.setFocusTarget(m_dsktWnd.getHWnd());

  SetTimer(m_hWnd, TIMER_DESKTOP_STATE, TIMER_DESKTOP_STATE_DELAY, (TIMERPROC)NULL);
}

ViewerWindow::~ViewerWindow()
{
  // Unregistration of keyboard hook.
  m_winHooks.unregisterKeyboardHook(this);

  if (m_ftDialog != 0) {
    try {
      delete m_ftDialog;
    } catch (...) {
    }
    m_ftDialog = 0;
  }
}

void ViewerWindow::setFileTransfer(FileTransferCapability *ft)
{
  m_fileTransfer = ft;
}

void ViewerWindow::setRemoteViewerCore(RemoteViewerCore *pCore)
{
  m_viewerCore = pCore;
  m_dsktWnd.setViewerCore(pCore);
  applySettings();
}

bool ViewerWindow::onCreate(LPCREATESTRUCT lps)
{
  // Dialogs of the session are owned by the frame.
  m_control.setWindow(m_host->rootHwnd());

  setClassCursor(LoadCursor(NULL, IDC_ARROW));
  // Hidden until the setting below says otherwise.
  m_toolbar.create(getHWnd(), this);
  // The session menu lives in the system menu of the frame, which forwards
  // its commands (onFrameCommand()).
  m_menu.getSystemMenu(m_host->rootHwnd());
  m_menu.loadMenu();
  addDisplayMenus();
  applySettings();

  ViewerConfig *config = ViewerConfig::getInstance();
  m_bToolBar = config->isToolbarShown();
  if (m_bToolBar) {
    m_toolbar.show();
  }
  m_menu.checkedMenuItem(IDS_TB_TOOLBAR, m_bToolBar);
  return true;
}

void ViewerWindow::enableUserElements()
{
  bool isEnable = !m_conConf->isViewOnly();

  UINT enableMenu = static_cast<UINT>(!isEnable);
  m_menu.enableMenuItem(IDS_TB_CTRLALTDEL, enableMenu);
  m_menu.enableMenuItem(IDS_TB_CTRLESC, enableMenu);
  m_menu.enableMenuItem(IDS_TB_CTRL, enableMenu);
  m_menu.enableMenuItem(IDS_TB_ALT, enableMenu);

  if (!isEnable) {
    m_menu.checkedMenuItem(IDS_TB_ALT, false);
    m_menu.checkedMenuItem(IDS_TB_CTRL, false);
  }
  updateToolbar();
}

bool ViewerWindow::viewerCoreSettings()
{
  if (!m_viewerCore) {
    return false;
  }

  bool bFileTransfer = m_fileTransfer && m_fileTransfer->isEnabled();

  UINT enableMenu = static_cast<UINT>(!(bFileTransfer && !m_conConf->isViewOnly()));
  m_menu.enableMenuItem(IDS_TB_TRANSFER, enableMenu);

  m_viewerCore->allowCopyRect(m_conConf->isCopyRectAllowed());
  m_viewerCore->setPreferredEncoding(m_conConf->getPreferredEncoding());

  m_viewerCore->ignoreCursorShapeUpdates(m_conConf->isIgnoringShapeUpdates());
  m_viewerCore->enableCursorShapeUpdates(m_conConf->isRequestingShapeUpdates());

  // set -1, if compression is disabled
  m_viewerCore->setCompressionLevel(m_conConf->getCustomCompressionLevel());

  // set -1, if jpeg-compression is disabled
  m_viewerCore->setJpegQualityLevel(m_conConf->getJpegCompressionLevel());

  if (m_conConf->isUsing8BitColor()) {
    const PixelFormat pixelFormat = StandardPixelFormatFactory::create8bppPixelFormat();
    m_viewerCore->setPixelFormat(&pixelFormat);
  } else {
    const PixelFormat pixelFormat = StandardPixelFormatFactory::create32bppPixelFormat();
    m_viewerCore->setPixelFormat(&pixelFormat);
  }
  return true;
}

void ViewerWindow::applySettings()
{
  // The remote screen always fits the window with its aspect ratio and no
  // scroll bars, like AnyDesk and RustDesk: a scale saved for the host, given
  // by -scale= or read from a .vnc file is ignored (see DesktopWindow).
  if (!m_conConf->isFitWindowEnabled()) {
    m_conConf->fitWindow(true);
  }
  if (m_isConnected) {
    if (m_conConf->isFullscreenEnabled()) {
      doFullScr();
    } else {
      doUnFullScr();
    }
  }
  changeCursor(m_conConf->getLocalCursorShape());
  enableUserElements();
  viewerCoreSettings();
  updateToolbar();
}

void ViewerWindow::changeCursor(int type)
{
  HCURSOR hcur = 0;

  ResourceLoader *rLoader = ResourceLoader::getInstance();
  switch (type) {
    case ConnectionConfig::DOT_CURSOR:
      hcur = rLoader->loadCursor(IDI_CDOT);
      break;
    case ConnectionConfig::SMALL_CURSOR:
      hcur = rLoader->loadCursor(IDI_CSMALLDOT);
      break;
    case ConnectionConfig::NO_CURSOR:
      hcur = rLoader->loadCursor(IDI_CNOCURSOR);
      break;
    case ConnectionConfig::NORMAL_CURSOR:
      hcur = rLoader->loadStandardCursor(IDC_ARROW);
      break;
  }
  // The cursor of the remote desktop is the class cursor of its window.
  if (m_dsktWnd.getHWnd() != 0) {
    m_dsktWnd.setClassCursor(hcur);
  }
}

bool ViewerWindow::onSysCommand(WPARAM wParam, LPARAM lParam)
{
  return onCommand(wParam, lParam);
}

bool ViewerWindow::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
  case WM_NCDESTROY:
    m_stopped = true;
    return true;
  case WM_USER_STOP:
    SendMessage(m_hWnd, WM_DESTROY, 0, 0);
    return true;
  case WM_USER_FS_WARNING:
    return onFsWarning();
  case WM_USER_CONNECTED:
    return onSessionShown();
  case WM_CLOSE:
    return onClose();
  case WM_DESTROY:
    return onDestroy();
  case WM_CREATE:
    return onCreate((LPCREATESTRUCT) lParam);
  case WM_SIZE:
    return onSize(wParam, lParam);
  case WM_USER_AUTH_ERROR:
    return onAuthError(wParam);
  case WM_USER_ERROR:
    return onError();
  case WM_USER_DISCONNECT:
    return onDisconnect();
  case WM_SETFOCUS:
    return onFocus(wParam);
  case WM_ERASEBKGND:
    return onEraseBackground((HDC)wParam);
  case WM_KILLFOCUS:
    return onKillFocus(wParam);
  case WM_TIMER:
    return onTimer(wParam);
  }
  return false;
}

bool ViewerWindow::onFrameCommand(WPARAM wParam, LPARAM lParam)
{
  return onCommand(wParam, lParam);
}

void ViewerWindow::onFrameActivate(WPARAM wParam)
{
  // The keyboard hook sends system shortcuts (Win, Alt+Tab, Ctrl+Esc...)
  // to the server while the viewer is the active window, windowed or not.
  if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
    // full screen viewer can be minimized from other screen
    if (IsIconic(m_host->rootHwnd())) {
      return;
    }
    try {
      // Registration of keyboard hook.
      m_winHooks.registerKeyboardHook(this);
      // Switching off ignoring win key.
      m_dsktWnd.setWinKeyIgnore(false);
    } catch (Exception &e) {
      m_logWriter.error(_T("%s"), e.getMessage());
    }
  } else if (LOWORD(wParam) == WA_INACTIVE) {
    // Unregistration of keyboard hook.
    m_winHooks.unregisterKeyboardHook(this);
    // Switching on ignoring win key.
    m_dsktWnd.setWinKeyIgnore(true);
  }
}

void ViewerWindow::onFrameSizing()
{
  m_sizeIsChanged = true;
}

void ViewerWindow::onFrameDisplayChange()
{
  onDisplayChange();
  adjustWindowSize();
}

void ViewerWindow::onFrameDpiChanged(UINT dpi, bool frameMoved)
{
  m_toolbar.setDpi(dpi);
  if (frameMoved) {
    // The frame is being moved between monitors: keep the size suggested by
    // the system and don't let adjustWindowSize() move it back to the center.
    m_sizeIsChanged = true;
  }
  m_dsktWnd.refreshLayout();
  doSize();
}

bool ViewerWindow::isFrameShown() const
{
  return !!IsWindowVisible(m_host->rootHwnd());
}

HWND ViewerWindow::getFocusHwnd() const
{
  return m_dsktWnd.getHWnd();
}

bool ViewerWindow::onEraseBackground(HDC hdc)
{
  return true;
}

bool ViewerWindow::onKillFocus(WPARAM wParam)
{
  // The Ctrl and Alt toggles of the toolbar follow the remote desktop
  // state (updateToolbar()).
  return true;
}

bool ViewerWindow::onTimer(WPARAM idTimer)
{
  switch (idTimer) {
  case TIMER_DESKTOP_STATE:
    desktopStateUpdate();
    return true;
  default:
    _ASSERT(false);
    return false;
  }
}

void ViewerWindow::dialogConnectionOptions()
{
  OptionsDialog dialog;

  dialog.setConnected();
  dialog.setConnectionConfig(m_conConf);
  // FIXME: Removed Control from this code and another
  Control control = m_host->rootHwnd();
  dialog.setParent(&control);

  if (dialog.showModal() == 1) {
    m_conConf->saveToStorage(&m_ccsm);
    applySettings();
  }
}

void ViewerWindow::dialogConnectionInfo()
{
  StringStorage host = m_conData->getHost();
  std::vector<TCHAR> kbdName;
  kbdName.resize(KL_NAMELENGTH);
  memset(&kbdName[0], 0, sizeof(TCHAR) * KL_NAMELENGTH);
  if (!GetKeyboardLayoutName( &kbdName[0] )) {
    kbdName[0] = _T('?');
    kbdName[1] = _T('?');
    kbdName[2] = _T('?');
  }

  Rect geometry;
  int pixelSize = 0;
  m_dsktWnd.getServerGeometry(&geometry, &pixelSize);
  StringStorage str;
  str.format(StringTable::getString(IDS_CONNECTION_INFO_FORMAT),
             host.getString(),
             m_viewerCore->getRemoteDesktopName().getString(),
             m_viewerCore->getProtocolString().getString(),
             geometry.getWidth(),
             geometry.getHeight(),
             pixelSize,
             &kbdName[0]);
  MessageBox(m_host->rootHwnd(),
             str.getString(),
             StringTable::getString(IDS_CONNECTION_INFO_CAPTION),
             MB_OK | MB_ICONINFORMATION);
}

void ViewerWindow::switchFullScreenMode()
{
  if (m_isFullScr) {
    m_logWriter.debug(_T("Switch to windowed mode"));
    doUnFullScr();
  } else {
    m_logWriter.debug(_T("Switch to full screen mode"));
    doFullScr();
  }
}

void ViewerWindow::dialogConfiguration()
{
  m_application->postMessage(TvnViewer::WM_USER_CONFIGURATION);
}

void ViewerWindow::desktopStateUpdate()
{
  // Adjust window of viewer to size of remote desktop.
  adjustWindowSize();

  // Update state of toolbar-buttons (Ctrl, Alt) to hardware-button state.
  updateKeyState();
}

// The key commands are refused in view-only mode, like the disabled
// buttons and menu items that send them.
void ViewerWindow::commandCtrlAltDel()
{
  if (!m_conConf->isViewOnly()) {
    m_dsktWnd.sendCtrlAltDel();
  }
}

void ViewerWindow::commandCtrlEsc()
{
  if (!m_conConf->isViewOnly()) {
    m_dsktWnd.sendKey(VK_LCONTROL, true);
    m_dsktWnd.sendKey(VK_ESCAPE,   true);
    m_dsktWnd.sendKey(VK_ESCAPE,   false);
    m_dsktWnd.sendKey(VK_LCONTROL, false);
  }
}

void ViewerWindow::commandWinKey()
{
  // Ignored like the physical Win key while the frame is not active
  // (RfbKeySym::setWinKeyIgnore()).
  if (!m_conConf->isViewOnly()) {
    m_dsktWnd.sendKey(VK_LWIN, true);
    m_dsktWnd.sendKey(VK_LWIN, false);
  }
}

void ViewerWindow::commandCtrl()
{
  if (m_conConf->isViewOnly()) {
    return;
  }
  // Toggles the latch of the remote desktop, which the toolbar shows.
  bool down = !m_dsktWnd.getCtrlState();
  m_menu.checkedMenuItem(IDS_TB_CTRL, down);
  m_dsktWnd.setCtrlState(down);
  m_dsktWnd.sendKey(VK_LCONTROL, down);
  updateToolbar();
}

void ViewerWindow::commandAlt()
{
  if (m_conConf->isViewOnly()) {
    return;
  }
  bool down = !m_dsktWnd.getAltState();
  m_menu.checkedMenuItem(IDS_TB_ALT, down);
  m_dsktWnd.setAltState(down);
  m_dsktWnd.sendKey(VK_LMENU, down);
  updateToolbar();
}

void ViewerWindow::commandPause()
{
  m_isPaused = !m_isPaused;
  m_viewerCore->stopUpdating(m_isPaused);
  updateToolbar();
}

void ViewerWindow::commandToolBar()
{
  // The full screen session has no toolbar (the menu item is disabled).
  if (m_isFullScr) {
    return;
  }
  m_bToolBar = !m_bToolBar;
  m_logWriter.debug(m_bToolBar ? _T("Show toolbar") : _T("Hide toolbar"));
  m_menu.checkedMenuItem(IDS_TB_TOOLBAR, m_bToolBar);
  if (m_bToolBar) {
    m_toolbar.show();
  } else {
    m_toolbar.hide();
  }
  doSize();
}

void ViewerWindow::commandNewConnection()
{
  m_application->postMessage(TvnViewer::WM_USER_SHOW_HOME);
}

void ViewerWindow::commandSaveSession()
{
  TCHAR fileName[MAX_PATH] = _T("");

  StringStorage filterVncFiles(StringTable::getString(IDS_SAVE_SESSION_FILTER_VNC_FILES));
  StringStorage filterAllFiles(StringTable::getString(IDS_SAVE_SESSION_FILTER_ALL_FILES));
  TCHAR vncMask[] = _T("*.vnc");
  TCHAR allMask[] = _T("*.*");


  vector<TCHAR> filter;
  filter.insert(filter.end(), filterVncFiles.getString(), filterVncFiles.getString() + filterVncFiles.getLength() + 1);
  filter.insert(filter.end(), vncMask, vncMask + sizeof(vncMask) / sizeof(TCHAR) - 1);
  filter.push_back(_T(';'));
  filter.push_back(_T('\0'));

  filter.insert(filter.end(), filterAllFiles.getString(), filterAllFiles.getString() + filterAllFiles.getLength() + 1);
  filter.insert(filter.end(), allMask, allMask + sizeof(allMask) / sizeof(TCHAR) - 1);
  filter.push_back(_T('\0'));
  filter.push_back(_T('\0'));


  OPENFILENAME ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = m_host->rootHwnd();
  ofn.lpstrFilter = &filter.front();
  ofn.lpstrDefExt = _T("vnc");
  ofn.lpstrFile= fileName;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_OVERWRITEPROMPT;
  try {
    if (GetSaveFileName(&ofn)) {
      File oldSettings(fileName);
      if (oldSettings.exists()) {
        oldSettings.remove();
      }
      IniFileSettingsManager sm(fileName);
      sm.setApplicationName(_T("connection"));

      StringStorage host;
      m_conData->getReducedHost(&host);
      sm.setString(_T("host"), host.getString());
      sm.setUINT(_T("port"), m_conData->getPort());

      if (m_conData->isSetPassword()) {
        int whetherToSavePass = MessageBox(m_host->rootHwnd(),
          StringTable::getString(IDS_QUESTION_SAVE_PASSWORD),
          StringTable::getString(IDS_SECURITY_WARNING_CAPTION),
          MB_YESNO);
        if (whetherToSavePass == IDYES) {
          StringStorage password = m_conData->getCryptedPassword();
          sm.setString(_T("password"), password.getString());
        }
      }

      sm.setApplicationName(_T("options"));
      m_conConf->saveToStorage(&sm);
    }
  } catch (...) {
    m_logWriter.error(_T("Error in save connection"));
  }
}

int ViewerWindow::translateAccelToTB(int val)
{
  static const std::pair<int, int> accelerators[] = {
    make_pair(ID_CONN_OPTIONS,    IDS_TB_CONNOPTIONS),
    make_pair(ID_CONN_INFO,       IDS_TB_CONNINFO),
    make_pair(ID_SHOW_TOOLBAR,    IDS_TB_TOOLBAR),
    make_pair(ID_FULL_SCR,        IDS_TB_FULLSCREEN),
    make_pair(ID_REQ_SCR_REFRESH, IDS_TB_REFRESH),
    make_pair(ID_CTRL_ALT_DEL,    IDS_TB_CTRLALTDEL),
    make_pair(ID_TRANSF_FILES,    IDS_TB_TRANSFER)
  };

  for (int i = 0; i < sizeof(accelerators) / sizeof(std::pair<int, int>); i++) {
    if (accelerators[i].first == val) {
      m_logWriter.debug(_T("accelerator pressed: %d"), val);
      return accelerators[i].second;
    }
  }
  return -1;
}

void ViewerWindow::onCheckForUpdates()
{
  m_application->postMessage(TvnViewer::WM_USER_CHECK_UPDATES);
}

void ViewerWindow::onAbout()
{
  m_application->postMessage(TvnViewer::WM_USER_ABOUT);
}

bool ViewerWindow::onCommand(WPARAM wParam, LPARAM lParam)
{
  if (HIWORD(wParam) == 1) {
    int transl = translateAccelToTB(LOWORD(wParam));

    if (transl != -1) {
      wParam = transl;
    }
  }
  UINT command = LOWORD(wParam);
  if (command >= 0xA000 && command < 0xA800 && (command & 15) == 0) {
    size_t index = (command - 0xA000) / 16;
    if (index < m_fullscreenMonitors.size()) {
      m_fullscreenMonitor = m_fullscreenMonitors[index];
      m_spanMonitors = false;
      applyFullscreenTarget();
    }
    return true;
  }
  if (command == 0xA800 || command == 0xA810) {
    m_spanMonitors = command == 0xA800;
    m_fullscreenMonitor = 0;
    applyFullscreenTarget();
    return true;
  }
  switch(wParam) {
    case IDS_ABOUT_VIEWER:
      onAbout();
      return true;
    case IDS_CHECK_FOR_UPDATES:
      onCheckForUpdates();
      return true;
    case IDS_TB_CONNOPTIONS:
      dialogConnectionOptions();
      return true;
    case IDS_TB_CONNINFO:
      dialogConnectionInfo();
      return true;
    case IDS_TB_PAUSE:
      commandPause();
      return true;
    case IDS_TB_REFRESH:
      m_viewerCore->refreshFrameBuffer();
      return true;
    case IDS_TB_CTRLALTDEL:
      commandCtrlAltDel();
      return true;
    case IDS_TB_CTRLESC:
      commandCtrlEsc();
      return true;
    case ID_SEND_WIN_KEY:
      commandWinKey();
      return true;
    case IDS_TB_CTRL:
      commandCtrl();
      return true;
    case IDS_TB_ALT:
      commandAlt();
      return true;
    case IDS_TB_TOOLBAR:
      commandToolBar();
      return true;
    case IDS_TB_TRANSFER:
      showFileTransferDialog();
      return true;
    case IDS_TB_NEWCONNECTION:
      commandNewConnection();
      return true;
    case IDS_TB_SAVESESSION:
      commandSaveSession();
      return true;
    case IDS_TB_FULLSCREEN:
      switchFullScreenMode();
      return true;
    case IDS_TB_CONFIGURATION:
      dialogConfiguration();
      return true;
  }
  return false;
}

void ViewerWindow::showFileTransferDialog()
{
  bool canTransfer = m_fileTransfer != 0 && m_fileTransfer->isEnabled() &&
                     !m_conConf->isViewOnly();
  if (canTransfer) {
    // FIXME: FT check it
    if (m_ftDialog != 0) {
      if (!m_ftDialog->isCreated()) {
        _ASSERT(m_fileTransfer != 0);
        m_fileTransfer->setInterface(0);
        delete m_ftDialog;
        m_ftDialog = 0;
      }
    }
    if (m_ftDialog == 0) {
      _ASSERT(m_fileTransfer != 0);
      m_ftDialog = new FileTransferMainDialog(m_fileTransfer->getCore());
      m_fileTransfer->setInterface(m_ftDialog);
    }
    m_ftDialog->show();
    HWND dialogWnd = m_ftDialog->getControl()->getWindow();
    m_application->addModelessDialog(dialogWnd);
  }
}

void ViewerWindow::applyScreenChanges(bool isFullScreen)
{
  m_isFullScr = isFullScreen;
  doSize();
  redraw();
}

void ViewerWindow::setSizeFullScreenWindow()
{
  // Get size of desktop.
  HMONITOR hmon = m_fullscreenMonitor ? m_fullscreenMonitor : MonitorFromWindow(m_host->rootHwnd(), MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);

  RECT fullScreenWindowsRect;
  if (m_spanMonitors) {
    fullScreenWindowsRect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    fullScreenWindowsRect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    fullScreenWindowsRect.right = fullScreenWindowsRect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    fullScreenWindowsRect.bottom = fullScreenWindowsRect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  } else if (!!GetMonitorInfo(hmon, &mi)) {
    fullScreenWindowsRect = mi.rcMonitor;
  } else {
    m_logWriter.warning(_T("Get monitor info is failed. Use second method (no multi-screen)."));
    GetWindowRect(GetDesktopWindow(), &fullScreenWindowsRect);
  }
  m_host->enterFullscreen(fullScreenWindowsRect);
}

void ViewerWindow::doFullScr()
{
  if (m_isFullScr) {
    return;
  }

  m_conConf->enableFullscreen(true);
  m_conConf->saveToStorage(&m_ccsm);

  ViewerConfig *config = ViewerConfig::getInstance();
  // The full screen session has no toolbar; m_bToolBar brings it back.
  m_toolbar.hide();

  m_menu.checkedMenuItem(IDS_TB_FULLSCREEN, true);
  m_menu.checkedMenuItem(IDS_TB_TOOLBAR,    false);
  m_menu.enableMenuItem(IDS_TB_TOOLBAR,     1);

  setSizeFullScreenWindow();

  SetFocus(m_dsktWnd.getHWnd());
  applyScreenChanges(true);

  m_fsToolbar.setLabel(fullScreenToolbarLabel().getString());
  m_fsToolbar.setState(toolbarState());
  m_fsToolbar.create(m_host->rootHwnd(), getHWnd(), this, m_dsktWnd.getHWnd(),
                     fullScreenToolbarMonitor());

  if (config->isPromptOnFullscreenEnabled()) {
    postMessage(WM_USER_FS_WARNING);
  }

  try {
    // Registration of keyboard hook.
    m_winHooks.registerKeyboardHook(this);
    // Switching off ignoring win key.
    m_dsktWnd.setWinKeyIgnore(false);
  } catch (Exception &e) {
    m_logWriter.error(_T("%s"), e.getMessage());
  }
}

void ViewerWindow::doUnFullScr()
{
  if (!m_isFullScr) {
    return;
  }

  m_fsToolbar.destroy();

  m_conConf->enableFullscreen(false);
  m_conConf->saveToStorage(&m_ccsm);

  m_menu.checkedMenuItem(IDS_TB_FULLSCREEN, false);
  m_menu.checkedMenuItem(IDS_TB_TOOLBAR, m_bToolBar);

  if (m_bToolBar) {
    m_toolbar.show();
  } else {
    m_toolbar.hide();
  }

  UINT isEnable = static_cast<UINT>(m_conConf->isViewOnly());
  m_menu.enableMenuItem(IDS_TB_TOOLBAR, isEnable);

  m_host->exitFullscreen();

  m_dsktWnd.refreshLayout();
  applyScreenChanges(false);

  // The windowed viewer keeps the keyboard hook while it is active.
  if (GetForegroundWindow() != m_host->rootHwnd()) {
    m_winHooks.unregisterKeyboardHook(this);
    m_dsktWnd.setWinKeyIgnore(true);
  }
}

bool ViewerWindow::onClose()
{
  if (m_ftDialog != 0 && m_ftDialog->isCreated()) {
    if (!m_ftDialog->tryClose()) {
      return true;
    }
  }
  endSession();
  return true;
}

bool ViewerWindow::onDestroy()
{
  KillTimer(m_hWnd, TIMER_DESKTOP_STATE);
  // The frame may outlive the session (reconnection): its full screen
  // toolbar goes with the session.
  m_fsToolbar.destroy();
  return true;
}

void ViewerWindow::endSession()
{
  m_dsktWnd.destroyWindow();
  m_host->onSessionEnded(m_requiresReconnect);
}

void ViewerWindow::postReconnect(const ConnectionData &conData)
{
  // The application keeps running until the new instance is created (see
  // m_requiresReconnect); the new session is shown in the same frame.
  m_requiresReconnect = true;
  TvnViewer::ReconnectRequest *request =
    new TvnViewer::ReconnectRequest(conData, *m_conConf, m_host->rootHwnd());
  m_application->postMessage(TvnViewer::WM_USER_RECONNECT, 0,
                             reinterpret_cast<LPARAM>(request));
}

void ViewerWindow::doSize()
{
  postMessage(WM_SIZE);
}

bool ViewerWindow::onSize(WPARAM wParam, LPARAM lParam)
{
  RECT rc;
  int x, y;

  getClientRect(&rc);
  m_logWriter.debug(_T("client rect: %d, %d; %d, %d"),
                    rc.left, rc.top, rc.right, rc.bottom);
  x = y = 0;
  if (m_toolbar.isVisible()) {
    // The toolbar on top, the remote desktop below it.
    y = m_toolbar.getHeight();
    SetWindowPos(m_toolbar.getHWnd(), 0, 0, 0, rc.right - rc.left, y,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    rc.bottom -= y;
  }
  if (m_dsktWnd.getHWnd()) {

    int h = rc.bottom - rc.top;
    int w = rc.right - rc.left;

    m_logWriter.debug(_T("Desktop-window. (x, y): (%d, %d); (w, h): (%d, %d)"),
                      x, y, w, h);
    if (h > 0 && w > 0) {
      m_dsktWnd.setPosition(x, y);
      m_dsktWnd.setSize(w, h);
    }
  }
  return true;
}

void ViewerWindow::showWindow()
{
  m_host->showFrame();

  StringStorage windowName = formatWindowTitle();
  StringStorage sessionName = formatSessionName();
  m_host->setTitle(&windowName, &sessionName);
  m_dsktWnd.setWindowText(&windowName);

}

void ViewerWindow::showConnecting()
{
  m_connectionState = STATE_CONNECTING;
  showWindow();
}

void ViewerWindow::setConnectionState(ConnectionState state)
{
  m_connectionState = state;
  StringStorage windowName = formatWindowTitle();
  StringStorage sessionName = formatSessionName();
  m_host->setTitle(&windowName, &sessionName);
  m_fsToolbar.setLabel(fullScreenToolbarLabel().getString());
}

const TCHAR *ViewerWindow::connectionStateLabel() const
{
  switch (m_connectionState) {
  case STATE_CONNECTING: return _T("Connecting...");
  case STATE_CONNECTED: return _T("Connected");
  default: return _T("Disconnected");
  }
}

StringStorage ViewerWindow::fullScreenToolbarLabel() const
{
  // The session name, and the connection state while it is not connected.
  StringStorage label = formatSessionName();
  if (m_connectionState != STATE_CONNECTED) {
    if (label.isEmpty()) {
      label.setString(connectionStateLabel());
    } else {
      label.appendString(_T(" \x00B7 "));
      label.appendString(connectionStateLabel());
    }
  }
  return label;
}

bool ViewerWindow::offerReconnect(bool sessionWasEstablished, const StringStorage *reason)
{
  ReconnectPolicy policy;
  ReconnectPolicy::Cause cause = sessionWasEstablished
                                 ? ReconnectPolicy::CAUSE_CONNECTION_LOST
                                 : ReconnectPolicy::CAUSE_CONNECT_FAILED;
  // A drop of an established session starts a new series of attempts.
  unsigned int attemptsMade = sessionWasEstablished ? 0 : m_conData->getReconnectAttempt();
  if (!policy.shouldOffer(cause, m_conData->isIncoming(), attemptsMade)) {
    return false;
  }
  m_logWriter.info(_T("Offering to reconnect (%u attempts made): %s"),
                   attemptsMade, reason->getString());

  setConnectionState(STATE_DISCONNECTED);
  if (m_isFullScr && m_fsToolbar.isCreated()) {
    m_fsToolbar.showOn(fullScreenToolbarMonitor());
  }

  bool autoRetry = policy.canRetryAutomatically(attemptsMade);
  StringStorage instruction;
  if (sessionWasEstablished) {
    instruction.setString(_T("The connection to the remote computer was lost."));
  } else {
    instruction.format(_T("Reconnection attempt %u failed."), attemptsMade);
  }
  StringStorage details = *reason;
  if (!autoRetry) {
    details.appendString(_T("\n\nNo automatic attempts are left."));
  }
  HWND owner = IsWindowVisible(m_host->rootHwnd()) ? m_host->rootHwnd() : 0;
  bool reconnect = ReconnectDialog::show(owner,
                                         formatWindowName().getString(),
                                         instruction.getString(),
                                         details.getString(),
                                         autoRetry ? policy.delayBeforeAttemptMs(attemptsMade) : 0,
                                         attemptsMade + 1,
                                         policy.getMaxAttempts());
  if (reconnect) {
    // Same host, options and in-memory password.
    ConnectionData connectionData(*m_conData);
    connectionData.setReconnectAttempt(attemptsMade + 1);
    postReconnect(connectionData);
  }
  endSession();
  return true;
}

bool ViewerWindow::onDisconnect()
{
  if (offerReconnect(m_isConnected, &m_disconnectMessage)) {
    return true;
  }
  MessageBox(m_host->rootHwnd(),
             m_disconnectMessage.getString(),
             formatWindowName().getString(),
             MB_OK);

  endSession();
  return true;
}

bool ViewerWindow::onAuthError(WPARAM wParam)
{
  // If authentication is canceled, then do quiet exit, else show error-message.
  if (wParam != AuthException::AUTH_CANCELED) {
    StringStorage error = m_error.getMessage();
    int result = MessageBox(0,
                            error.getString(),
                            formatWindowName().getString(),
                            MB_RETRYCANCEL | MB_ICONERROR);
    if (result == IDRETRY) {
      if (!m_conData->isIncoming()) {
        // Retry connect to remote host.
        ConnectionData connectionData(*m_conData);
        connectionData.resetPassword();
        connectionData.setReconnectAttempt(0);
        postReconnect(connectionData);
      }
    }
  }
  endSession();
  return true;
}

bool ViewerWindow::onError()
{
  // Errors of an established session are not connection drops.
  if (!m_isConnected) {
    StringStorage reason(m_error.getMessage());
    if (offerReconnect(false, &reason)) {
      return true;
    }
  }
  StringStorage error;
  error.format(_T("Error in %s: %s"), ProductNames::VIEWER_PRODUCT_NAME, m_error.getMessage());
  MessageBox(m_host->rootHwnd(),
             error.getString(),
             formatWindowName().getString(),
             MB_OK | MB_ICONERROR);

  endSession();
  return true;
}

bool ViewerWindow::onFsWarning()
{
  FsWarningDialog fsWarning;
  fsWarning.setParent(&m_control);
  fsWarning.showModal();
  return true;
}

bool ViewerWindow::onFocus(WPARAM wParam)
{
  SetFocus(m_dsktWnd.getHWnd());
  return true;
}

void ViewerWindow::onBell()
{
  if (m_conConf->isDeiconifyOnRemoteBellEnabled()) {
    ShowWindow(m_host->rootHwnd(), SW_RESTORE);
    SetForegroundWindow(m_host->rootHwnd());
  }
  MessageBeep(MB_ICONASTERISK);
}

void ViewerWindow::onConnected(RfbOutputGate *output)
{
  // Set flags.
  m_isConnected = true;
  m_connectionState = STATE_CONNECTED;
  m_conData->setReconnectAttempt(0);
  m_sizeIsChanged = false;
  m_dsktWnd.setConnected();

  // Set output for client-to-server messages in file transfer.
  m_fileTransfer->setOutput(output);

  // Update list of supported operation for file transfer.
  vector<UINT32> clientMsgCodes;
  m_viewerCore->getEnabledClientMsgCapabilities(&clientMsgCodes);

  vector<UINT32> serverMsgCodes;
  m_viewerCore->getEnabledServerMsgCapabilities(&serverMsgCodes);

  m_fileTransfer->getCore()->updateSupportedOperations(&clientMsgCodes, &serverMsgCodes);

  // This runs in the thread of the viewer core, which pumps no messages.
  // Full screen created the floating toolbar and the low level keyboard hook
  // in it, so the window thread blocked on the toolbar and the keyboard
  // stalled: the session opened in full screen froze. Windows are touched
  // in the window thread only.
  postMessage(WM_USER_CONNECTED);
}

bool ViewerWindow::onSessionShown()
{
  if (m_stopped || !m_isConnected) {
    return true;
  }
  // Start viewer window and applying settings.
  showWindow();
  // Like AnyDesk and RustDesk, a session opens maximized (the remote
  // desktop is scaled to the window by default, see ConnectionConfig); a
  // reconnected session keeps the frame of the previous one.
  if (m_isFirstConnection) {
    m_isFirstConnection = false;
    if (!m_conConf->isFullscreenEnabled()) {
      // The user's size from now on: adjustWindowSize() must not shrink it.
      m_sizeIsChanged = true;
      m_host->openMaximized();
    }
  }
  SetForegroundWindow(m_host->rootHwnd());
  applySettings();
  return true;
}

void ViewerWindow::onDisconnect(const StringStorage *message)
{
  m_logWriter.info(_T("onDisconnect: %s"), message->getString());
  m_disconnectMessage = *message;
  if (!m_stopped) {
    postMessage(WM_USER_DISCONNECT);
  }
}

void ViewerWindow::onAuthError(const AuthException *exception)
{
  m_logWriter.info(_T("onAuthError (%d): %s"),
                   exception->getAuthCode(), exception->getMessage());
  int authCode = exception->getAuthCode();
  m_error = *exception;
  postMessage(WM_USER_AUTH_ERROR, authCode);
}

void ViewerWindow::onError(const Exception *exception)
{
  m_error = *exception;
  postMessage(WM_USER_ERROR);
}

void ViewerWindow::onFrameBufferUpdate(const FrameBuffer *fb, const Rect *rect)
{
  m_dsktWnd.updateFramebuffer(fb, rect);
}

void ViewerWindow::onFrameBufferPropChange(const FrameBuffer *fb)
{
  m_dsktWnd.setNewFramebuffer(fb);
}

void ViewerWindow::onCutText(const StringStorage *cutText)
{
  m_dsktWnd.setClipboardData(cutText);
}

void ViewerWindow::doCommand(int iCommand)
{
  postMessage(WM_COMMAND, iCommand);
}

bool ViewerWindow::requiresReconnect() const
{
  return m_requiresReconnect;
}

bool ViewerWindow::isStopped() const
{
  return m_stopped;
}

void ViewerWindow::adjustWindowSize()
{
  // If size isn't changed by user, then adjust size. Before the first
  // framebuffer (a reconnection attempt shows the window early) there is
  // nothing to fit.
  if (!m_sizeIsChanged && m_dsktWnd.getViewerGeometry().area() != 0) {
    // The windowed frame fits the remote screen and the toolbar (the frame
    // keeps that size until the remote screen size changes).
    if (!m_isFullScr) {
      Rect viewerRect = m_dsktWnd.getViewerGeometry();
      int clientHeight = viewerRect.getHeight() + 1;
      if (m_toolbar.isVisible()) {
        clientHeight += m_toolbar.getHeight();
      }
      m_host->requestClientSize(viewerRect.getWidth(), clientHeight);
    }

    // This is done for keyboard hooks to work.
    // If m_conConf->isFullscreenEnabled() is true,
    // hooks don't work at the first start of the viewer.
    if (m_hooksEnabledFirstTime && GetForegroundWindow() == m_host->rootHwnd()) {
      try {
        // Registration of keyboard hook.
        m_winHooks.registerKeyboardHook(this);
        // Switching off ignoring win key.
        m_dsktWnd.setWinKeyIgnore(false);
        m_hooksEnabledFirstTime = false;
      } catch (Exception &e) {
        m_logWriter.error(_T("%s"), e.getMessage());
      }
    }
  }
}

void ViewerWindow::updateKeyState()
{
  // The Ctrl and Alt toggles follow the keys held on the remote desktop.
  updateToolbar();
}

SessionToolbarState ViewerWindow::toolbarState() const
{
  SessionToolbarState state;
  bool canType = m_isConnected && !m_conConf->isViewOnly();

  state.enabled.set();
  state.checked[SESSION_ACTION_FULL_SCREEN] = m_isFullScr;

  state.enabled[SESSION_ACTION_CTRL_ALT_DEL] = canType;
  state.enabled[SESSION_ACTION_WIN_KEY] = canType;
  state.enabled[SESSION_ACTION_CTRL_LATCH] = canType;
  state.enabled[SESSION_ACTION_ALT_LATCH] = canType;
  state.enabled[SESSION_ACTION_SPECIAL_KEYS] = canType;
  state.checked[SESSION_ACTION_CTRL_LATCH] = canType && m_dsktWnd.getCtrlState();
  state.checked[SESSION_ACTION_ALT_LATCH] = canType && m_dsktWnd.getAltState();

  state.enabled[SESSION_ACTION_FILE_TRANSFER] = canType && m_fileTransfer != 0 &&
                                                m_fileTransfer->isEnabled();
  state.enabled[SESSION_ACTION_PAUSE] = m_isConnected;
  state.enabled[SESSION_ACTION_REFRESH] = m_isConnected;
  state.checked[SESSION_ACTION_PAUSE] = m_isPaused;
  return state;
}

void ViewerWindow::updateToolbar()
{
  SessionToolbarState state = toolbarState();
  m_toolbar.setState(state);
  m_fsToolbar.setState(state);
}

namespace {

void appendCommand(HMENU menu, UINT id, bool enabled = true, bool checked = false)
{
  StringStorage text;
  ResourceLoader::getInstance()->loadString(id, &text);
  AppendMenu(menu, MF_STRING | (enabled ? 0 : MF_GRAYED) | (checked ? MF_CHECKED : 0),
             id, text.getString());
}

}

HMENU ViewerWindow::createMonitorMenu()
{
  // The monitors may have changed since the last WM_DISPLAYCHANGE.
  refreshMonitorMenu();
  HMENU menu = CreatePopupMenu();
  AppendMenu(menu, MF_STRING, 0xA810, _T("Full screen on the current monitor"));
  AppendMenu(menu, MF_STRING | (m_isFullScr && m_spanMonitors ? MF_CHECKED : 0),
             0xA800, _T("Full screen across all monitors"));
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  for (size_t i = 0; i < m_fullscreenMonitors.size(); i++) {
    MONITORINFOEX info;
    info.cbSize = sizeof(info);
    if (!GetMonitorInfo(m_fullscreenMonitors[i], &info)) {
      continue;
    }
    StringStorage label;
    label.format(_T("Monitor %u (%d x %d)%s"), (unsigned)i + 1,
                 (int)(info.rcMonitor.right - info.rcMonitor.left),
                 (int)(info.rcMonitor.bottom - info.rcMonitor.top),
                 (info.dwFlags & MONITORINFOF_PRIMARY) != 0 ? _T(", primary") : _T(""));
    bool selected = m_isFullScr && !m_spanMonitors &&
                    m_fullscreenMonitors[i] == m_fullscreenMonitor;
    AppendMenu(menu, MF_STRING | (selected ? MF_CHECKED : 0), 0xA000 + i * 16,
               label.getString());
  }
  return menu;
}

HMENU ViewerWindow::createSpecialKeysMenu()
{
  bool canType = !m_conConf->isViewOnly();
  HMENU menu = CreatePopupMenu();
  appendCommand(menu, IDS_TB_CTRLESC, canType);
  AppendMenu(menu, MF_STRING | (canType ? 0 : MF_GRAYED), ID_SEND_WIN_KEY, _T("Send Win key"));
  appendCommand(menu, IDS_TB_CTRLALTDEL, canType);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  appendCommand(menu, IDS_TB_CTRL, canType, canType && m_dsktWnd.getCtrlState());
  appendCommand(menu, IDS_TB_ALT, canType, canType && m_dsktWnd.getAltState());
  return menu;
}

HMENU ViewerWindow::createMoreMenu()
{
  // The session commands that have no button of their own (the system menu
  // has them all too).
  HMENU menu = CreatePopupMenu();
  appendCommand(menu, IDS_TB_NEWCONNECTION);
  appendCommand(menu, IDS_TB_SAVESESSION);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  appendCommand(menu, IDS_TB_CONNOPTIONS);
  appendCommand(menu, IDS_TB_CONNINFO, m_isConnected);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  appendCommand(menu, IDS_TB_TOOLBAR, !m_isFullScr, m_bToolBar && !m_isFullScr);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  appendCommand(menu, IDS_TB_CONFIGURATION);
  appendCommand(menu, IDS_CHECK_FOR_UPDATES);
  appendCommand(menu, IDS_ABOUT_VIEWER);
  return menu;
}

void ViewerWindow::onToolbarMenu(SessionAction action, const RECT &anchor)
{
  HMENU menu = 0;
  switch (action) {
  case SESSION_ACTION_MONITOR:
    menu = createMonitorMenu();
    break;
  case SESSION_ACTION_SPECIAL_KEYS:
    menu = createSpecialKeysMenu();
    break;
  case SESSION_ACTION_MORE:
    menu = createMoreMenu();
    break;
  default:
    return;
  }
  if (menu == 0) {
    return;
  }
  // The menu runs a modal loop in which this session may end: only locals
  // are used after it.
  HWND self = m_hWnd;
  HWND owner = m_host->rootHwnd();
  if (GetForegroundWindow() != owner) {
    // A popup menu needs a foreground owner to close when the user clicks
    // elsewhere.
    SetForegroundWindow(owner);
  }
  TPMPARAMS params;
  params.cbSize = sizeof(params);
  params.rcExclude = anchor;
  UINT command = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL |
                                  TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                  anchor.left, anchor.bottom, owner, &params);
  DestroyMenu(menu);
  if (command != 0 && IsWindow(self)) {
    PostMessage(self, WM_COMMAND, command, 0);
  }
}

StringStorage ViewerWindow::formatSessionName() const
{
  StringStorage desktopName = m_viewerCore->getRemoteDesktopName();
  if (desktopName.isEmpty() && !m_conData->getHost().isEmpty()) {
    desktopName = m_conData->getHost();
  }
  return desktopName;
}

StringStorage ViewerWindow::formatWindowName() const
{
  StringStorage desktopName = formatSessionName();
  StringStorage windowName;
  if (!desktopName.isEmpty()) {
    windowName.format(_T("%s | %s"), ProductNames::VIEWER_PRODUCT_NAME, desktopName.getString());
  } else {
    windowName.format(_T("%s"), ProductNames::VIEWER_PRODUCT_NAME);
  }
  return windowName;
}

StringStorage ViewerWindow::formatWindowTitle() const
{
  StringStorage windowName = formatWindowName();
  if (m_connectionState == STATE_CONNECTING) {
    windowName.appendString(_T(" (connecting...)"));
  } else if (m_connectionState == STATE_DISCONNECTED) {
    windowName.appendString(_T(" (disconnected)"));
  }
  return windowName;
}

LRESULT ViewerWindow::onHookProc(int code, WPARAM wParam, LPARAM lParam)
{
  HWND focus = GetFocus();
  if (GetForegroundWindow() != m_host->rootHwnd() ||
      (focus != m_dsktWnd.getHWnd() && !IsChild(m_dsktWnd.getHWnd(), focus))) {
    return false;
  }
  // A windowed viewer that cannot type into the server leaves the shortcuts
  // (Alt+Tab, Win...) to the local desktop.
  if (!m_isFullScr && (!m_isConnected || m_conConf->isViewOnly())) {
    return false;
  }
  KBDLLHOOKSTRUCT *str = (KBDLLHOOKSTRUCT*) lParam;
  if (WinHooks::decideKeyAction(str->vkCode, str->flags) == WinHooks::HOOK_KEY_PASS) {
    return false;
  }
  LPARAM newLParam = WinHooks::makeKeyMessageLParam(str->scanCode, str->flags);
  if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN ||
      wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
    PostMessage(m_dsktWnd.getHWnd(), wParam, str->vkCode, newLParam);
  }
  return true;
}

BOOL CALLBACK ViewerWindow::collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
  ViewerWindow *viewer = reinterpret_cast<ViewerWindow *>(data);
  viewer->m_fullscreenMonitors.push_back(monitor);
  return viewer->m_fullscreenMonitors.size() < 64;
}

void ViewerWindow::addDisplayMenus()
{
  m_displayMenu = CreatePopupMenu();
  AppendMenu(m_displayMenu, MF_STRING, 0xA810, _T("Fullscreen on current monitor"));
  AppendMenu(m_displayMenu, MF_STRING, 0xA800, _T("Fullscreen across all monitors"));
  m_fixedDisplayMenuItems = GetMenuItemCount(m_displayMenu);
  refreshMonitorMenu();
  InsertMenu(m_menu.getMenu(), 0, MF_BYPOSITION | MF_POPUP,
             reinterpret_cast<UINT_PTR>(m_displayMenu), _T("Display"));
}

void ViewerWindow::refreshMonitorMenu()
{
  // The list of monitors changes when a monitor is plugged or unplugged.
  while (GetMenuItemCount(m_displayMenu) > m_fixedDisplayMenuItems) {
    DeleteMenu(m_displayMenu, m_fixedDisplayMenuItems, MF_BYPOSITION);
  }
  m_fullscreenMonitors.clear();
  EnumDisplayMonitors(0, 0, collectMonitor, reinterpret_cast<LPARAM>(this));
  bool selectedMonitorExists = false;
  for (size_t i = 0; i < m_fullscreenMonitors.size(); i++) {
    MONITORINFOEX info;
    info.cbSize = sizeof(info);
    if (GetMonitorInfo(m_fullscreenMonitors[i], &info)) {
      StringStorage label;
      label.format(_T("Fullscreen monitor %u (%s)"), (unsigned)i + 1, info.szDevice);
      AppendMenu(m_displayMenu, MF_STRING, 0xA000 + i * 16, label.getString());
    }
    selectedMonitorExists = selectedMonitorExists || m_fullscreenMonitors[i] == m_fullscreenMonitor;
  }
  if (!selectedMonitorExists) {
    m_fullscreenMonitor = 0;
  }
}

void ViewerWindow::applyFullscreenTarget()
{
  if (m_isFullScr) {
    setSizeFullScreenWindow();
    doSize();
    m_fsToolbar.showOn(fullScreenToolbarMonitor());
  } else {
    doFullScr();
  }
}

void ViewerWindow::onDisplayChange()
{
  if (m_displayMenu != 0) {
    refreshMonitorMenu();
  }
  if (m_isFullScr) {
    // Keep covering the selected monitor(s) after a resolution or layout change.
    setSizeFullScreenWindow();
    doSize();
    m_fsToolbar.showOn(fullScreenToolbarMonitor());
  }
}

HMONITOR ViewerWindow::fullScreenToolbarMonitor()
{
  // Across all monitors the toolbar starts on the monitor of the pointer;
  // the hot strip of any monitor brings it there later.
  if (m_spanMonitors) {
    POINT pt;
    if (GetCursorPos(&pt)) {
      return MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    }
  }
  return MonitorFromWindow(m_host->rootHwnd(), MONITOR_DEFAULTTONEAREST);
}

void ViewerWindow::onDesktopPointerMove(POINT clientPoint)
{
  if (!m_isFullScr || !m_fsToolbar.isCreated()) {
    return;
  }
  POINT screenPoint = clientPoint;
  if (ClientToScreen(m_dsktWnd.getHWnd(), &screenPoint)) {
    m_fsToolbar.onPointerMove(screenPoint);
  }
}