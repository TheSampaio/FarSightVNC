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
// Modified for FarSightVNC on 2026-11-03: connection state label.
// Modified for FarSightVNC on 2026-11-11: rewritten as a floating pill drawn
// with ChromeRenderer; the buttons are the SessionActionStrip of the session
// toolbar.
//

#include "FullScreenToolbar.h"
#include "gui/DpiUtils.h"

namespace {

const wchar_t kWindowClassName[] = L"FarSightVncFullScreenToolbar";

const DWORD kStyle = WS_POPUP;
const DWORD kExStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

// Sizes in DIPs.
const int HOT_STRIP_HEIGHT = 3;      // Pixels at 96 DPI.
const int NEAR_MARGIN = 24;          // Pixels at 96 DPI.
const float kPillHeight = 40.0f;
const float kPillPadding = 10.0f;    // Inside the rounded ends.
const float kTopGap = 8.0f;          // Between the monitor edge and the pill.
const float kShadow = 12.0f;         // Room for the shadow on the sides and below.
const float kScreenMargin = 16.0f;   // Minimum room on each side of the monitor.
const float kLabelMaxWidth = 200.0f;
const float kLabelMinWidth = 40.0f;
const float kLabelGap = 8.0f;
const float kDividerHeight = 16.0f;
const float kHandleWidth = 64.0f;
const float kHandleHeight = 8.0f;
const float kHandleMargin = 4.0f;    // Room for the shadow of the handle.
const ThemeColor kShadowColor = 0x000000;

D2D1_RECT_F inflate(const D2D1_RECT_F &rect, float dx, float dy)
{
  return D2D1::RectF(rect.left - dx, rect.top - dy, rect.right + dx, rect.bottom + dy);
}

}

FullScreenToolbar::FullScreenToolbar()
: m_hwnd(0),
  m_owner(0),
  m_commandTarget(0),
  m_listener(0),
  m_monitor(0),
  m_inLayout(false),
  m_expanded(false),
  m_strip(SESSION_PLACEMENT_PILL),
  m_pill(D2D1::RectF()),
  m_labelRect(D2D1::RectF()),
  m_labelDivider(D2D1::RectF())
{
  m_sessionState.enabled.set();
}

FullScreenToolbar::~FullScreenToolbar()
{
  destroy();
}

bool FullScreenToolbar::registerClass()
{
  WNDCLASSEXW wc;
  if (GetClassInfoExW(GetModuleHandle(0), kWindowClassName, &wc)) {
    return true;
  }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = windowProc;
  wc.hInstance = GetModuleHandle(0);
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.lpszClassName = kWindowClassName;
  return RegisterClassExW(&wc) != 0;
}

void FullScreenToolbar::create(HWND owner, HWND commandTarget, SessionToolbarListener *listener,
                               HWND focusTarget, HMONITOR monitor)
{
  if (m_hwnd != 0 || !registerClass()) {
    return;
  }
  m_owner = owner;
  m_commandTarget = commandTarget;
  m_listener = listener;
  m_monitor = 0;
  m_expanded = false;
  m_logic = FullScreenToolbarLogic();
  // Owned (not child) popup: it stays above the topmost full screen shell
  // and is hidden and destroyed together with it.
  CreateWindowExW(kExStyle, kWindowClassName, L"FarSightVNC full screen toolbar", kStyle,
                  0, 0, 1, 1, owner, 0, GetModuleHandle(0), this);
  if (m_hwnd == 0) {
    return;
  }
  m_chrome.attachLayered(m_hwnd, DpiUtils::getDpiForWindow(m_hwnd));
  m_strip.attach(m_hwnd, &m_chrome, m_hwnd, this, [this]() { render(); });
  m_strip.setFocusTarget(focusTarget);
  applyState();
  showOn(monitor);
}

void FullScreenToolbar::destroy()
{
  if (m_hwnd != 0) {
    DestroyWindow(m_hwnd);
  }
}

void FullScreenToolbar::setState(const SessionToolbarState &state)
{
  m_sessionState = state;
  applyState();
}

void FullScreenToolbar::applyState()
{
  SessionToolbarState state = m_sessionState;
  state.enabled[SESSION_ACTION_PIN] = true;
  state.enabled[SESSION_ACTION_EXIT_FULL_SCREEN] = true;
  state.enabled[SESSION_ACTION_DISCONNECT] = true;
  state.checked[SESSION_ACTION_PIN] = m_logic.isPinned();
  state.checked[SESSION_ACTION_EXIT_FULL_SCREEN] = false;
  state.checked[SESSION_ACTION_DISCONNECT] = false;
  m_strip.setState(state);
}

void FullScreenToolbar::setLabel(const TCHAR *text)
{
  std::wstring label = text != 0 ? text : L"";
  if (label == m_label) {
    return;
  }
  m_label = label;
  if (m_hwnd != 0 && m_expanded && m_monitor != 0) {
    layout(m_monitor);
  }
}

void FullScreenToolbar::layout(HMONITOR monitor)
{
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  if (m_hwnd == 0 || !GetMonitorInfo(monitor, &mi)) {
    return;
  }
  m_inLayout = true;
  const ThemeTokens &theme = kLightTheme;
  const RECT &rcMon = mi.rcMonitor;
  int centerX = rcMon.left + (rcMon.right - rcMon.left) / 2;
  // Move onto the monitor first: the window takes the DPI of that monitor.
  SetWindowPos(m_hwnd, HWND_TOPMOST, centerX, rcMon.top, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
  m_chrome.setDpi(DpiUtils::getDpiForWindow(m_hwnd));

  float width;
  float height;
  if (m_expanded) {
    float monitorWidth = m_chrome.pixelsToDips(rcMon.right - rcMon.left);
    float maxPill = monitorWidth - 2.0f * (kScreenMargin + kShadow);
    float stripWidth = SessionActionStrip::fullWidth(SESSION_PLACEMENT_PILL);
    float labelWidth = 0.0f;
    if (!m_label.empty()) {
      labelWidth = m_chrome.measureText(m_label.c_str(), theme.fontSizeSmall,
                                        DWRITE_FONT_WEIGHT_SEMI_BOLD) + 4.0f;
      if (labelWidth > kLabelMaxWidth) {
        labelWidth = kLabelMaxWidth;
      }
    }
    // The label gives way to the buttons on a narrow monitor.
    float labelBlock = labelWidth > 0.0f ? labelWidth + 2.0f * kLabelGap + 1.0f : 0.0f;
    float excess = 2.0f * kPillPadding + labelBlock + stripWidth - maxPill;
    if (excess > 0.0f && labelWidth > 0.0f) {
      labelWidth -= excess;
      if (labelWidth < kLabelMinWidth) {
        labelWidth = 0.0f;
      }
      labelBlock = labelWidth > 0.0f ? labelWidth + 2.0f * kLabelGap + 1.0f : 0.0f;
    }
    float pillWidth = 2.0f * kPillPadding + labelBlock + stripWidth;
    if (pillWidth > maxPill) {
      pillWidth = maxPill;
    }
    m_pill = D2D1::RectF(kShadow, kTopGap, kShadow + pillWidth, kTopGap + kPillHeight);
    float x = m_pill.left + kPillPadding;
    if (labelWidth > 0.0f) {
      m_labelRect = D2D1::RectF(x, m_pill.top, x + labelWidth, m_pill.bottom);
      x += labelWidth + kLabelGap;
      float dividerTop = m_pill.top + (kPillHeight - kDividerHeight) / 2.0f;
      m_labelDivider = D2D1::RectF(x, dividerTop, x + 1.0f, dividerTop + kDividerHeight);
      x += 1.0f + kLabelGap;
    } else {
      m_labelRect = D2D1::RectF();
      m_labelDivider = D2D1::RectF();
    }
    m_strip.layout(x, m_pill.right - kPillPadding, m_pill.top, m_pill.bottom);
    width = pillWidth + 2.0f * kShadow;
    height = kTopGap + kPillHeight + kShadow;
  } else {
    m_pill = D2D1::RectF(kHandleMargin, 0.0f, kHandleMargin + kHandleWidth, kHandleHeight);
    width = kHandleWidth + 2.0f * kHandleMargin;
    height = kHandleHeight + kHandleMargin;
  }

  int pixelWidth = m_chrome.dipsToPixels(width);
  int pixelHeight = m_chrome.dipsToPixels(height);
  SetWindowPos(m_hwnd, HWND_TOPMOST, centerX - pixelWidth / 2, rcMon.top,
               pixelWidth, pixelHeight, SWP_NOACTIVATE);
  m_chrome.resize(pixelWidth, pixelHeight);
  m_monitor = monitor;
  m_inLayout = false;
  render();
}

void FullScreenToolbar::render()
{
  if (m_hwnd == 0 || m_inLayout || m_monitor == 0 || !m_chrome.beginDraw()) {
    return;
  }
  const ThemeTokens &theme = kLightTheme;
  float line = m_chrome.pixelsToDips(1);
  m_chrome.clearTransparent();
  if (m_expanded) {
    // Soft shadow: rings of low opacity below the pill.
    const int rings = 8;
    const float step = 1.25f;
    float radius = kPillHeight / 2.0f;
    for (int i = rings; i >= 1; i--) {
      float grow = i * step;
      D2D1_RECT_F ring = inflate(m_pill, grow, grow);
      ring.top += 2.0f;
      ring.bottom += 2.0f;
      m_chrome.fillRoundRect(ring, radius + grow, kShadowColor, 0.022f);
    }
    m_chrome.fillRoundRect(m_pill, radius, theme.surface);
    m_chrome.drawRoundRect(m_pill, radius, theme.border, line);
    if (m_labelRect.right > m_labelRect.left) {
      m_chrome.drawText(m_label.c_str(), m_labelRect, theme.text, theme.fontSizeSmall,
                        DWRITE_FONT_WEIGHT_SEMI_BOLD);
      D2D1_RECT_F divider = m_labelDivider;
      divider.right = divider.left + line;
      m_chrome.fillRect(divider, theme.border);
    }
    m_strip.paint();
  } else {
    // Grab handle: a small tab hanging from the top edge, with a grip line.
    float radius = kHandleHeight / 2.0f;
    for (int i = 3; i >= 1; i--) {
      D2D1_RECT_F ring = inflate(m_pill, i * 1.0f, 0.0f);
      ring.top -= kHandleHeight;
      ring.bottom += i * 1.0f;
      m_chrome.fillRoundRect(ring, radius + i, kShadowColor, 0.05f);
    }
    D2D1_RECT_F tab = m_pill;
    tab.top -= kHandleHeight;
    m_chrome.fillRoundRect(tab, radius, theme.surface);
    m_chrome.drawRoundRect(tab, radius, theme.border, line);
    float middle = (m_pill.left + m_pill.right) / 2.0f;
    float gripY = m_pill.bottom / 2.0f;
    m_chrome.fillRoundRect(D2D1::RectF(middle - 12.0f, gripY - 1.0f, middle + 12.0f, gripY + 1.0f),
                           1.0f, theme.mutedText);
  }
  m_chrome.endDraw();
}

void FullScreenToolbar::showOn(HMONITOR monitor)
{
  expand(monitor);
}

void FullScreenToolbar::expand(HMONITOR monitor)
{
  if (m_hwnd == 0 || monitor == 0) {
    return;
  }
  bool wasExpanded = m_expanded;
  m_expanded = true;
  if (!wasExpanded || monitor != m_monitor || !IsWindowVisible(m_hwnd)) {
    layout(monitor);
  }
  m_logic.show(GetTickCount());
  SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(m_hwnd, TIMER_AUTO_HIDE, TIMER_AUTO_HIDE_PERIOD, 0);
}

void FullScreenToolbar::collapse()
{
  KillTimer(m_hwnd, TIMER_AUTO_HIDE);
  m_logic.hide();
  m_expanded = false;
  if (m_monitor != 0) {
    layout(m_monitor);
  }
}

int FullScreenToolbar::nearMargin() const
{
  return DpiUtils::scale(NEAR_MARGIN, m_chrome.getDpi());
}

FullScreenToolbarLogic::Box FullScreenToolbar::toolbarBox() const
{
  RECT rc;
  if (!m_expanded || !IsWindowVisible(m_hwnd) || !GetWindowRect(m_hwnd, &rc)) {
    FullScreenToolbarLogic::Box empty = { 0, 0, 0, 0 };
    return empty;
  }
  FullScreenToolbarLogic::Box box = { rc.left, rc.top, rc.right, rc.bottom };
  return box;
}

bool FullScreenToolbar::isPointerNear() const
{
  POINT pt;
  if (!m_expanded || !IsWindowVisible(m_hwnd) || !GetCursorPos(&pt)) {
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

void FullScreenToolbar::onToolbarMenu(SessionAction action, const RECT &anchor)
{
  // The auto-hide keeps the pill while the menu is open (isMenuOpen()).
  if (m_listener != 0) {
    m_listener->onToolbarMenu(action, anchor);
  }
}

void FullScreenToolbar::onCommand(WPARAM wParam, LPARAM lParam)
{
  int id = LOWORD(wParam);
  if (id == ID_PILL_PIN) {
    m_logic.setPinned(!m_logic.isPinned(), GetTickCount());
    applyState();
    return;
  }
  // A modal dialog of the viewer (options, warnings) disables the owner.
  if (!IsWindowEnabled(m_owner)) {
    return;
  }
  if (id == ID_PILL_DISCONNECT) {
    PostMessage(m_owner, WM_CLOSE, 0, 0);
  } else {
    // Leaving full screen destroys this window: the session handles the
    // command later.
    PostMessage(m_commandTarget, WM_COMMAND, wParam, lParam);
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
    onCommand(wParam, lParam);
    return 0;
  case WM_TIMER:
    if (wParam == TIMER_AUTO_HIDE) {
      if (m_logic.onTimer(isPointerNear() || m_strip.isMenuOpen(), GetTickCount())) {
        collapse();
      }
      return 0;
    }
    break;
  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN:
    if (!m_expanded) {
      // The handle brings the pill back.
      expand(m_monitor);
      return 0;
    }
    break;
  case WM_DPICHANGED:
    // layout() moves the window and reads the DPI itself; outside of it (DPI
    // setting changed) the pill is laid out again on the same monitor.
    if (!m_inLayout && m_monitor != 0) {
      layout(m_monitor);
    }
    return 0;
  case WM_DESTROY:
    KillTimer(hwnd, TIMER_AUTO_HIDE);
    return 0;
  case WM_NCDESTROY:
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    m_strip.detach();
    m_hwnd = 0;
    m_monitor = 0;
    m_expanded = false;
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  if (m_expanded && m_strip.onMouseMessage(message, wParam, lParam)) {
    return 0;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}
