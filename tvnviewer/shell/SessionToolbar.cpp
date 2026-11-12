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

#include "SessionToolbar.h"

#include "gui/d2d/Theme.h"

#include "gui/DpiUtils.h"

namespace {

const wchar_t kWindowClassName[] = L"FsvSessionToolbar";

const wchar_t kGlyphLock = 0xE72E;

// Sizes in DIPs.
const float kLockSize = 14.0f;
const float kLatencyWidth = 44.0f;

}

SessionToolbar::SessionToolbar()
: m_hwnd(0),
  m_listener(0),
  m_strip(SESSION_PLACEMENT_BAR),
  m_latency(D2D1::RectF()),
  m_lock(D2D1::RectF()),
  m_showConnection(false)
{
}

SessionToolbar::~SessionToolbar()
{
  destroy();
}

bool SessionToolbar::registerClass()
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

bool SessionToolbar::create(HWND parent, SessionToolbarListener *listener)
{
  if (m_hwnd != 0 || !registerClass()) {
    return m_hwnd != 0;
  }
  m_listener = listener;
  CreateWindowExW(0, kWindowClassName, L"Session toolbar",
                  WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, 0, GetModuleHandle(0), this);
  if (m_hwnd == 0) {
    return false;
  }
  m_chrome.attach(m_hwnd, DpiUtils::getDpiForWindow(m_hwnd));
  m_strip.attach(m_hwnd, &m_chrome, parent, listener, [this]() { invalidate(); });
  return true;
}

void SessionToolbar::destroy()
{
  if (m_hwnd != 0) {
    DestroyWindow(m_hwnd);
  }
}

LRESULT CALLBACK SessionToolbar::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  SessionToolbar *toolbar;
  if (message == WM_NCCREATE) {
    CREATESTRUCT *create = reinterpret_cast<CREATESTRUCT *>(lParam);
    toolbar = static_cast<SessionToolbar *>(create->lpCreateParams);
    toolbar->m_hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(toolbar));
  } else {
    toolbar = reinterpret_cast<SessionToolbar *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (toolbar == 0) {
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    toolbar->m_hwnd = 0;
    toolbar->m_strip.detach();
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  return toolbar->onMessage(message, wParam, lParam);
}

LRESULT SessionToolbar::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
  case WM_MOUSEACTIVATE:
    // A click on the bar never activates the frame nor moves the focus.
    return MA_NOACTIVATE;
  case WM_SIZE:
    m_chrome.resize(LOWORD(lParam), HIWORD(lParam));
    layout();
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint();
    return 0;
  }
  if (m_strip.onMouseMessage(message, wParam, lParam)) {
    return 0;
  }
  return DefWindowProc(m_hwnd, message, wParam, lParam);
}

int SessionToolbar::getHeight() const
{
  return m_chrome.dipsToPixels(currentTheme().toolbarHeight);
}

void SessionToolbar::setDpi(UINT dpi)
{
  if (dpi == 0 || dpi == m_chrome.getDpi()) {
    return;
  }
  m_chrome.setDpi(dpi);
  layout();
}

void SessionToolbar::setState(const SessionToolbarState &state)
{
  m_strip.setState(state);
}

void SessionToolbar::show()
{
  if (m_hwnd != 0) {
    ShowWindow(m_hwnd, SW_SHOWNA);
  }
}

void SessionToolbar::hide()
{
  if (m_hwnd != 0) {
    ShowWindow(m_hwnd, SW_HIDE);
  }
}

bool SessionToolbar::isVisible() const
{
  return m_hwnd != 0 && (GetWindowLong(m_hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
}

void SessionToolbar::layout()
{
  if (m_hwnd == 0) {
    return;
  }
  const ThemeTokens &theme = currentTheme();
  RECT client;
  GetClientRect(m_hwnd, &client);
  float width = m_chrome.pixelsToDips(client.right - client.left);
  float height = theme.toolbarHeight;
  float top = (height - theme.buttonSize) / 2.0f;
  float bottom = top + theme.buttonSize;

  // Connection area on the right: latency text and lock glyph.
  m_lock = D2D1::RectF(width - theme.spacing - kLockSize - theme.spacingSmall, top,
                       width - theme.spacing, bottom);
  m_latency = D2D1::RectF(m_lock.left - kLatencyWidth, top, m_lock.left, bottom);

  float end = m_strip.layout(theme.spacing, width - theme.spacing, 0.0f, height);
  m_showConnection = end + theme.spacingLarge <= m_latency.left;
  invalidate();
}

void SessionToolbar::invalidate()
{
  if (m_hwnd != 0) {
    InvalidateRect(m_hwnd, 0, FALSE);
  }
}

void SessionToolbar::paint()
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(m_hwnd, &ps);
  if (dc != 0) {
    const ThemeTokens &theme = currentTheme();
    if (m_chrome.beginDraw()) {
      RECT client;
      GetClientRect(m_hwnd, &client);
      float width = m_chrome.pixelsToDips(client.right - client.left);
      float height = m_chrome.pixelsToDips(client.bottom - client.top);
      m_chrome.clear(theme.background);
      // Hairline between the bar and the remote desktop.
      float line = m_chrome.pixelsToDips(1);
      m_chrome.fillRect(D2D1::RectF(0.0f, height - line, width, height), theme.border);

      m_strip.paint();
      if (m_showConnection) {
        // Placeholders until the connection metrics exist.
        m_chrome.drawText(L"\x2014 ms", m_latency, theme.mutedText, theme.fontSizeSmall,
                          DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (!m_chrome.drawIconGlyph(kGlyphLock, m_lock, theme.mutedText, kLockSize - 2.0f)) {
          m_chrome.drawText(L"\x25A3", m_lock, theme.mutedText, theme.fontSizeSmall,
                            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
      }
      m_chrome.endDraw();
    } else {
      // No Direct2D: a plain bar.
      RECT area;
      GetClientRect(m_hwnd, &area);
      FillRect(dc, &area, GetSysColorBrush(COLOR_BTNFACE));
    }
  }
  EndPaint(m_hwnd, &ps);
}
