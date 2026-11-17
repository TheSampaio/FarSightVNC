// Copyright (C) 2012 GlavSoft LLC.
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
// Modified for FarSightVNC on 2026-11-02: optional Direct3D 11 / Direct2D presenter member.
// Modified for FarSightVNC on 2026-11-03: listener for local pointer movement (full screen toolbar).
// Modified for FarSightVNC on 2026-11-11: the remote screen always fits the window (no scale modes, no scroll bars).
// Modified for FarSightVNC on 2026-11-14: scan code of a dropped local key-down (its WM_CHAR is dropped too).
// Modified for FarSightVNC on 2026-11-16: Ctrl+V over files on the local clipboard becomes a clipboard file paste.
// Modified for FarSightVNC on 2026-11-16: releaseRemoteKeys() for the keyboard release toggle.
//

#ifndef _DESKTOP_WINDOW_H_
#define _DESKTOP_WINDOW_H_

#include "win-system/WinClipboard.h"
#include "gui/DibFrameBuffer.h"
#include "region/Rect.h"
#include "region/Dimension.h"
#include "ScaleManager.h"
#include "client-config-lib/ConnectionConfig.h"
#include "gui/PaintWindow.h"
#include "gui/ScrollBar.h"
#include "gui/drawing/SolidBrush.h"
#include "gui/drawing/Graphics.h"
#include "rfb/RfbKeySym.h"
#include "viewer-core/ClipboardFilesCapability.h"
#include "viewer-core/RemoteViewerCore.h"
#include "GpuPresenter.h"

#include <bitset>
#include <memory>
#include <vector>

// Receives the local pointer position over the desktop window (client
// coordinates), also in view-only mode and before it is sent to the server.
class DesktopPointerListener
{
public:
  virtual void onDesktopPointerMove(POINT clientPoint) = 0;
protected:
  ~DesktopPointerListener() {}
};

// Lets the desktop window turn a Ctrl+V over files on the local clipboard
// into a clipboard file paste (local machine -> remote machine). The window
// only decides whether to swallow the keystroke; the session window runs the
// paste and asks for the keystroke again when the remote clipboard is set.
class DesktopFilePasteListener
{
public:
  // True when the server advertised the clipboard file paste capability.
  virtual bool isFilePasteSupported() const = 0;
  // True when the server advertised the file transfer, which stages the
  // files on the remote machine.
  virtual bool isFileTransferSupported() const = 0;
  // True while a clipboard file paste of this session is still running.
  virtual bool isFilePasteInProgress() const = 0;
  // True while the file transfer dialog is using the file transfer core.
  virtual bool isFileTransferBusy() const = 0;
  // Starts the paste of the files on the local clipboard. Returns false when
  // nothing was started; the keystroke is then let through unchanged.
  virtual bool startFilePaste(const std::vector<StringStorage> *localFiles) = 0;

protected:
  ~DesktopFilePasteListener() {}
};

class DesktopWindow : public PaintWindow,
                      protected RfbKeySymListener
{
public:
  DesktopWindow(LogWriter *logWriter, ConnectionConfig *conConf);
  virtual ~DesktopWindow();

  void setClipboardData(const StringStorage *strText);
  void updateFramebuffer(const FrameBuffer *framebuffer,
                         const Rect *dstRect);
  // this function must be called if size of image was changed
  // or the number of bits per pixel
  void setNewFramebuffer(const FrameBuffer *framebuffer);

  // The image always fits the window (aspect ratio kept, letterboxed with
  // the background color, never scroll bars). Lays it out again, after a
  // change of the window that WM_SIZE does not report (DPI, full screen).
  void refreshLayout();
  // it returns the image width and height considering scale
  Rect getViewerGeometry();
  // it returns the image width and height.
  Rect getFrameBufferGeometry();
  // it return size of server frame buffer and pixelsize.
  void getServerGeometry(Rect *rect, int *pixelsize);

  void setConnected();
  void setViewerCore(RemoteViewerCore *viewerCore);

  // This function set state key "Ctrl", but not send data to server.
  void setCtrlState(const bool ctrlState);
  // This function set state key "Alt", but not send data to server.
  void setAltState(const bool altState);
  // This function return true, if key "Control" is pressed.
  bool getCtrlState() const;
  // This function return true, if key "Alt" is pressed.
  bool getAltState() const;

  // this function sends to remote viewer core
  // key what is pressed or unpressed
  void sendKey(WCHAR key, bool pressed);
  // this function sends to remote viewer core the combination
  // Ctrl + Alt + Del
  void sendCtrlAltDel();

  // Releases on the server every key it still has pressed and forgets the
  // Ctrl/Alt latches, exactly like a focus loss. Used by the keyboard release
  // toggle: the user may hold Ctrl when they hand the keyboard back to their
  // own machine, and the key-up would then never reach the server. Nothing is
  // pressed again afterwards (RfbKeySym::processFocusLoss() also makes the
  // next focus restoration press nothing).
  void releaseRemoteKeys();
  void setKeyboardReleased(bool released);

  // Set function for m_winKeyIgnore.
  void setWinKeyIgnore(bool winKeyIgnore) { m_rfbKeySym->setWinKeyIgnore(winKeyIgnore); }

  void setPointerListener(DesktopPointerListener *listener) { m_pointerListener = listener; }

  void setFilePasteListener(DesktopFilePasteListener *listener) { m_filePasteListener = listener; }

  // Posted by the clipboard file paste once the remote clipboard holds the
  // staged files: the window then synthesizes the Ctrl+V it swallowed.
  static const UINT WM_USER_FILE_PASTE = WM_USER + 1;

  // Sends a complete Ctrl+V to the server (Ctrl down, V down, V up, Ctrl
  // up), so that no modifier is left pressed on the remote machine even
  // though the real Ctrl was held while the files were staged.
  void sendCtrlV();

protected:
  //
  // Overrides RfbKeySymListener::onRfbKeySymEvent().
  //
  void onRfbKeySymEvent(unsigned int rfbKeySym, bool down);

  //
  // Inherited from BaseWindow.
  //
  bool onMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void onPaint(DeviceContext *dc, PAINTSTRUCT *paintStruct);
  bool onCreate(LPCREATESTRUCT pcs);
  bool onDrawClipboard();
  bool onEraseBackground(HDC hdc);
  bool onDeadChar(WPARAM wParam, LPARAM lParam);
  bool onHScroll(WPARAM wParam, LPARAM lParam);
  bool onVScroll(WPARAM wParam, LPARAM lParam);
  bool onKey(WPARAM wParam, LPARAM lParam);
  bool onChar(WPARAM wParam, LPARAM lParam);
  bool onMouse(unsigned char mouseKeys, unsigned short wheelSpeed, POINT position);
  bool onSize(WPARAM wParam, LPARAM lParam);
  bool onDestroy();

  POINTS getViewerCoord(long xPos, long yPos);
  void calculateWndSize(bool isChanged);
  void applyScrollbarChanges(bool isChanged, bool isVert, bool isHorz, int wndWidth, int wndHeight);

  // This function check pointer to viewer core and send event.
  // If into viewer core throwing exception Exception, then it catching
  // in this function and logged.
  void sendKeyboardEvent(bool downFlag, UINT32 key);
  void sendPointerEvent(UINT8 buttonMask, const Point *position);
  void sendCutTextEvent(const StringStorage *cutText);

  LogWriter *m_logWriter;

  // This variable is true after call CoreEventsAdapter::onConnected().
  bool m_isConnected;

  // keyboard support
  std::unique_ptr<RfbKeySym> m_rfbKeySym;

  // This variable contained previously state of mouse-button and position of cursor.
  unsigned char m_previousMouseState;
  Point m_previousMousePos;

  // scroll bars: vertical and horizontal
  ScrollBar m_sbar;

  // This flag is true if size of window is changed (scroll must be updated).
  bool m_winResize;
  bool m_showVert;
  bool m_showHorz;

  // for scaling
  ScaleManager m_scManager;
  Rect m_clientArea;
  int m_fbWidth;
  int m_fbHeight;
  SolidBrush m_brush;

  // frame buffer
  LocalMutex m_bufferLock;
  DibFrameBuffer m_framebuffer;
  // Direct3D 11 / Direct2D presenter; null when the GDI renderer is forced.
  std::unique_ptr<GpuPresenter> m_gpu;
  // This variable save server dimension.
  // Dimension of m_framebuffer can be large m_serverDimension.
  Dimension m_serverDimension;

  // clipboard
  WinClipboard m_clipboard;
  StringStorage m_strClipboard;
  HWND m_hwndNextViewer;

  bool m_ctrlDown;
  bool m_altDown;
  bool m_keyboardReleased;
  // Keys Windows holds when capture resumes stay local until their key-up.
  std::bitset<256> m_keysHeldLocally;
  // Scan code (bits 16-24 of lParam: scan code and extended flag) of the
  // local key whose key-down was dropped, or -1. TranslateMessage still
  // makes a WM_CHAR of it ('-' for Numpad Minus), dropped as well.
  int m_droppedLocalScan;

  RemoteViewerCore *m_viewerCore;
  ConnectionConfig *m_conConf;
  DesktopFilePasteListener *m_filePasteListener;
  bool m_isBackgroundDirty;
  DesktopPointerListener *m_pointerListener;

private:
  void doDraw(DeviceContext *dc);
  void scrollProcessing(int fbWidth, int fbHeight);
  void drawBackground(DeviceContext *dc, const RECT *rcMain, const RECT *rcImage);
  void drawImage(const RECT *src, const RECT *dst);
  void repaint(const Rect *repaintRect);
  void calcClientArea();
  // True when this key-down was a plain Ctrl+V over files on the local
  // clipboard and a clipboard file paste was started for it. The key-down
  // must then be swallowed.
  bool tryStartFilePaste();
};

#endif
