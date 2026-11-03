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

#include "FullScreenToolbar.h"
#include "gui/DpiUtils.h"
#include "resource.h"

#include <CommCtrl.h>

const TCHAR FullScreenToolbar::WINDOW_CLASS_NAME[] = _T("FarSightVncFullScreenToolbar");

namespace {
// Sizes for 96 DPI.
const int HOT_STRIP_HEIGHT = 3;
const int NEAR_MARGIN = 24;
const int FONT_POINTS = 9;

const int TOOLBAR_CONTROL_ID = 1;
}

FullScreenToolbar::FullScreenToolbar()
: m_hwnd(0),
  m_owner(0),
  m_monitor(0),
  m_dpi(0),
  m_font(0),
  m_inLayout(false)
{
}

FullScreenToolbar::~FullScreenToolbar()
{
  destroy();
}

bool FullScreenToolbar::registerClass()
{
  WNDCLASSEX wc;
  if (GetClassInfoEx(GetModuleHandle(0), WINDOW_CLASS_NAME, &wc)) {
    return true;
  }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = windowProc;
  wc.hInstance = GetModuleHandle(0);
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = WINDOW_CLASS_NAME;
  return RegisterClassEx(&wc) != 0;
}

void FullScreenToolbar::create(HWND owner, HMONITOR monitor)
{
  if (m_hwnd != 0 || !registerClass()) {
    return;
  }
  m_owner = owner;
  m_monitor = 0;
  m_dpi = 0;
  m_logic = FullScreenToolbarLogic();
  // Owned (not child) popup: it stays above the topmost full screen viewer
  // and is hidden and destroyed together with it.
  CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                 WINDOW_CLASS_NAME, _T("FarSightVNC full screen toolbar"),
                 WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
                 0, 0, 1, 1, owner, 0, GetModuleHandle(0), this);
  if (m_hwnd == 0) {
    return;
  }
  createButtons();
  showOn(monitor);
}

void FullScreenToolbar::destroy()
{
  if (m_hwnd != 0) {
    DestroyWindow(m_hwnd);
  }
}

void FullScreenToolbar::createButtons()
{
  m_toolbar.reset(new ToolBar());
  m_toolbar->loadToolBarfromRes(IDB_TOOLBAR);
  m_toolbar->create(TOOLBAR_CONTROL_ID, m_hwnd,
                    WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST |
                    TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE |
                    CCS_NOPARENTALIGN);
  HWND bar = toolbarControl();
  SendMessage(bar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);

  // Images come from the main toolbar bitmap (one image per IDS_TB_* id).
  struct ButtonInfo { int id; bool image; BYTE style; };
  const ButtonInfo buttons[] = {
    { IDS_TB_FULLSCREEN, true, BTNS_BUTTON },
    { 0, false, BTNS_SEP },
    { IDS_TB_CTRLALTDEL, true, BTNS_BUTTON },
    { IDS_TB_CONNOPTIONS, true, BTNS_BUTTON },
    { 0, false, BTNS_SEP },
    { ID_PIN, false, BTNS_CHECK | BTNS_SHOWTEXT },
    { ID_DISCONNECT, false, BTNS_BUTTON | BTNS_SHOWTEXT },
  };
  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
    TBBUTTON tbb;
    ZeroMemory(&tbb, sizeof(tbb));
    tbb.idCommand = buttons[i].id;
    tbb.fsStyle = buttons[i].style;
    if (buttons[i].style != BTNS_SEP) {
      tbb.fsStyle |= BTNS_AUTOSIZE;
      tbb.fsState = TBSTATE_ENABLED;
      tbb.iBitmap = buttons[i].image ? buttons[i].id - IDS_TB_NEWCONNECTION : I_IMAGENONE;
      // With TBSTYLE_EX_MIXEDBUTTONS the text of image buttons is their tooltip.
      tbb.iString = reinterpret_cast<INT_PTR>(tooltipText(buttons[i].id));
    }
    SendMessage(bar, TB_ADDBUTTONS, 1, reinterpret_cast<LPARAM>(&tbb));
  }
}

const TCHAR *FullScreenToolbar::tooltipText(int id) const
{
  switch (id) {
  case IDS_TB_FULLSCREEN: return _T("Leave full screen");
  case IDS_TB_CTRLALTDEL: return _T("Send Ctrl+Alt+Del");
  case IDS_TB_CONNOPTIONS: return _T("Connection options");
  case ID_PIN: return _T("Pin");
  case ID_DISCONNECT: return _T("Disconnect");
  }
  return _T("");
}

void FullScreenToolbar::applyDpi(UINT dpi)
{
  if (dpi == m_dpi || !m_toolbar) {
    return;
  }
  m_dpi = dpi;
  m_toolbar->setDpi(dpi);

  HFONT font = CreateFont(-MulDiv(FONT_POINTS, dpi, 72), 0, 0, 0, FW_NORMAL,
                          FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                          _T("Segoe UI"));
  if (font != 0) {
    SendMessage(toolbarControl(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (m_font != 0) {
      DeleteObject(m_font);
    }
    m_font = font;
  }
  SendMessage(toolbarControl(), TB_AUTOSIZE, 0, 0);
}

void FullScreenToolbar::layout(HMONITOR monitor)
{
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  if (m_hwnd == 0 || !GetMonitorInfo(monitor, &mi)) {
    return;
  }
  m_inLayout = true;
  const RECT &rcMon = mi.rcMonitor;
  int centerX = rcMon.left + (rcMon.right - rcMon.left) / 2;
  // Move onto the monitor first: the window takes the DPI of that monitor.
  SetWindowPos(m_hwnd, HWND_TOPMOST, centerX, rcMon.top, 1, 1,
               SWP_NOACTIVATE | SWP_NOSIZE);
  applyDpi(DpiUtils::getDpiForWindow(m_hwnd));

  HWND bar = toolbarControl();
  SIZE size = { 0, 0 };
  SendMessage(bar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&size));
  int pad = DpiUtils::scale(2, m_dpi);
  SetWindowPos(bar, 0, pad, pad, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);

  RECT rc = { 0, 0, size.cx + 2 * pad, size.cy + 2 * pad };
  DpiUtils::adjustWindowRect(&rc, WS_POPUP | WS_BORDER,
                             WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, m_dpi);
  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;
  SetWindowPos(m_hwnd, HWND_TOPMOST, centerX - width / 2, rcMon.top, width, height,
               SWP_NOACTIVATE);
  m_monitor = monitor;
  m_inLayout = false;
}

void FullScreenToolbar::showOn(HMONITOR monitor)
{
  if (m_hwnd == 0 || monitor == 0) {
    return;
  }
  if (monitor != m_monitor || !IsWindowVisible(m_hwnd)) {
    layout(monitor);
  }
  m_logic.show(GetTickCount());
  SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(m_hwnd, TIMER_AUTO_HIDE, TIMER_AUTO_HIDE_PERIOD, 0);
}

void FullScreenToolbar::hideNow()
{
  KillTimer(m_hwnd, TIMER_AUTO_HIDE);
  m_logic.hide();
  ShowWindow(m_hwnd, SW_HIDE);
}

HWND FullScreenToolbar::toolbarControl() const
{
  return GetDlgItem(m_hwnd, TOOLBAR_CONTROL_ID);
}

int FullScreenToolbar::nearMargin() const
{
  return DpiUtils::scale(NEAR_MARGIN, m_dpi != 0 ? m_dpi : DpiUtils::DEFAULT_DPI);
}

FullScreenToolbarLogic::Box FullScreenToolbar::toolbarBox() const
{
  RECT rc;
  if (!IsWindowVisible(m_hwnd) || !GetWindowRect(m_hwnd, &rc)) {
    FullScreenToolbarLogic::Box empty = { 0, 0, 0, 0 };
    return empty;
  }
  FullScreenToolbarLogic::Box box = { rc.left, rc.top, rc.right, rc.bottom };
  return box;
}

bool FullScreenToolbar::isPointerNear() const
{
  POINT pt;
  if (!IsWindowVisible(m_hwnd) || !GetCursorPos(&pt)) {
    return false;
  }
  return FullScreenToolbarLogic::isNear(toolbarBox(), nearMargin(), pt.x, pt.y);
}

void FullScreenToolbar::onPointerMove(POINT pt)
{
  if (m_hwnd == 0) {
    return;
  }
  HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfo(monitor, &mi)) {
    return;
  }
  FullScreenToolbarLogic::Box monitorBox = { mi.rcMonitor.left, mi.rcMonitor.top,
                                             mi.rcMonitor.right, mi.rcMonitor.bottom };
  // The strip follows the DPI of the full screen (owner) window.
  int strip = DpiUtils::scale(HOT_STRIP_HEIGHT, DpiUtils::getDpiForWindow(m_owner));
  if (m_logic.onPointerMove(monitorBox, strip, toolbarBox(), nearMargin(),
                            pt.x, pt.y, GetTickCount())) {
    showOn(monitor);
  }
}

void FullScreenToolbar::enableButton(int id, bool enable)
{
  if (m_toolbar) {
    m_toolbar->enableButton(id, enable);
  }
}

void FullScreenToolbar::onCommand(int id)
{
  // A modal dialog of the viewer (options, warnings) disables the owner.
  if (!IsWindowEnabled(m_owner)) {
    return;
  }
  switch (id) {
  case ID_PIN:
    m_logic.setPinned(m_toolbar->getState(ID_PIN) & TBSTATE_CHECKED ? true : false,
                      GetTickCount());
    break;
  case ID_DISCONNECT:
    PostMessage(m_owner, WM_CLOSE, 0, 0);
    break;
  case IDS_TB_FULLSCREEN:
  case IDS_TB_CTRLALTDEL:
  case IDS_TB_CONNOPTIONS:
    // Posted: leaving full screen destroys this window.
    PostMessage(m_owner, WM_COMMAND, id, 0);
    break;
  }
}

LRESULT CALLBACK FullScreenToolbar::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  FullScreenToolbar *self;
  if (message == WM_NCCREATE) {
    LPCREATESTRUCT cs = reinterpret_cast<LPCREATESTRUCT>(lParam);
    self = reinterpret_cast<FullScreenToolbar *>(cs->lpCreateParams);
    self->m_hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<FullScreenToolbar *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (self == 0) {
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  return self->onMessage(message, wParam, lParam);
}

LRESULT FullScreenToolbar::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  HWND hwnd = m_hwnd;
  switch (message) {
  case WM_MOUSEACTIVATE:
    // Clicks must leave the activation and the keyboard focus to the viewer.
    return MA_NOACTIVATE;
  case WM_COMMAND:
    onCommand(LOWORD(wParam));
    return 0;
  case WM_NOTIFY:
    {
      LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
      if (hdr->code == TTN_GETDISPINFO) {
        LPNMTTDISPINFO info = reinterpret_cast<LPNMTTDISPINFO>(lParam);
        // The text is copied: the control may modify it (it strips '&').
        _tcsncpy_s(info->szText, sizeof(info->szText) / sizeof(info->szText[0]),
                   tooltipText(static_cast<int>(hdr->idFrom)), _TRUNCATE);
        info->lpszText = info->szText;
        info->hinst = 0;
        return 0;
      }
    }
    break;
  case WM_TIMER:
    if (wParam == TIMER_AUTO_HIDE) {
      if (m_logic.onTimer(isPointerNear(), GetTickCount())) {
        hideNow();
      }
      return 0;
    }
    break;
  case WM_DPICHANGED:
    // layout() moves the window and reads the DPI itself; outside of it (DPI
    // setting changed) the toolbar is laid out again on the same monitor.
    if (!m_inLayout && m_monitor != 0) {
      layout(m_monitor);
    }
    return 0;
  case WM_DESTROY:
    // The toolbar control is still alive here; destroy it through its owner object.
    KillTimer(hwnd, TIMER_AUTO_HIDE);
    m_toolbar.reset();
    if (m_font != 0) {
      DeleteObject(m_font);
      m_font = 0;
    }
    return 0;
  case WM_NCDESTROY:
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    m_hwnd = 0;
    m_monitor = 0;
    m_dpi = 0;
    break;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}
