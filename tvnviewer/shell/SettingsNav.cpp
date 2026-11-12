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

#include "SettingsNav.h"

#include "gui/d2d/Theme.h"

#include "gui/DpiUtils.h"

namespace {

const wchar_t kWindowClassName[] = L"FsvSettingsNav";

// Layout in DIPs.
const float kItemHeight = 36.0f;
const float kItemGap = 4.0f;
const float kItemPadding = 12.0f;
const float kGlyphBox = 16.0f;
const float kIndicatorWidth = 3.0f;
const float kIndicatorHeight = 16.0f;

D2D1_RECT_F inflate(const D2D1_RECT_F &rect, float amount)
{
  return D2D1::RectF(rect.left - amount, rect.top - amount,
                     rect.right + amount, rect.bottom + amount);
}

}

SettingsNav::SettingsNav(HWND parent, UINT controlId, Mode mode, const std::vector<Item> &items)
: m_hwnd(0),
  m_id(controlId),
  m_mode(mode),
  m_items(items),
  m_selected(0),
  m_hover(-1),
  m_pressed(-1),
  m_trackingMouse(false)
{
  if (!registerClass()) {
    return;
  }
  const wchar_t *text = m_items.empty() ? L"" : m_items[0].label.c_str();
  CreateWindowExW(0, kWindowClassName, text,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  0, 0, 0, 0, parent,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
                  GetModuleHandle(0), this);
  if (m_hwnd != 0) {
    m_chrome.attach(m_hwnd, DpiUtils::getDpiForWindow(m_hwnd));
  }
}

SettingsNav::~SettingsNav()
{
  if (m_hwnd != 0) {
    DestroyWindow(m_hwnd);
  }
}

bool SettingsNav::registerClass()
{
  WNDCLASSEXW wc;
  if (GetClassInfoExW(GetModuleHandle(0), kWindowClassName, &wc)) {
    return true;
  }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = windowProc;
  wc.hInstance = GetModuleHandle(0);
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.lpszClassName = kWindowClassName;
  return RegisterClassExW(&wc) != 0;
}

float SettingsNav::itemPitch()
{
  return kItemHeight + kItemGap;
}

float SettingsNav::contentHeight() const
{
  return m_items.empty() ? 0.0f : m_items.size() * itemPitch() - kItemGap;
}

void SettingsNav::select(size_t index)
{
  if (index >= m_items.size() || m_mode != MODE_LIST) {
    return;
  }
  m_selected = index;
  SetWindowTextW(m_hwnd, m_items[index].label.c_str());
  invalidate();
}

void SettingsNav::setDpi(UINT dpi)
{
  if (dpi != 0 && dpi != m_chrome.getDpi()) {
    m_chrome.setDpi(dpi);
    invalidate();
  }
}

LRESULT CALLBACK SettingsNav::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  SettingsNav *nav;
  if (message == WM_NCCREATE) {
    CREATESTRUCT *create = reinterpret_cast<CREATESTRUCT *>(lParam);
    nav = static_cast<SettingsNav *>(create->lpCreateParams);
    nav->m_hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(nav));
  } else {
    nav = reinterpret_cast<SettingsNav *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (nav == 0) {
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    nav->m_hwnd = 0;
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  return nav->onMessage(message, wParam, lParam);
}

LRESULT SettingsNav::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
  case WM_SIZE:
    m_chrome.resize(LOWORD(lParam), HIWORD(lParam));
    invalidate();
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint();
    return 0;
  case WM_GETDLGCODE:
  {
    // The dialog keys of the page (Tab, Esc) stay with the page; the list
    // takes the arrows, and both take Enter and Space.
    LRESULT code = m_mode == MODE_LIST ? DLGC_WANTARROWS : 0;
    const MSG *msg = reinterpret_cast<const MSG *>(lParam);
    if (msg != 0 && msg->message == WM_KEYDOWN &&
        (msg->wParam == VK_RETURN || msg->wParam == VK_SPACE)) {
      code |= DLGC_WANTMESSAGE;
    }
    return code;
  }
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
    invalidate();
    return 0;
  case WM_KEYDOWN:
    switch (wParam) {
    case VK_UP:
    case VK_LEFT:
      moveSelection(static_cast<int>(m_selected) - 1);
      return 0;
    case VK_DOWN:
    case VK_RIGHT:
      moveSelection(static_cast<int>(m_selected) + 1);
      return 0;
    case VK_HOME:
      moveSelection(0);
      return 0;
    case VK_END:
      moveSelection(static_cast<int>(m_items.size()) - 1);
      return 0;
    case VK_RETURN:
    case VK_SPACE:
      notify(m_mode == MODE_LIST ? NOTIFY_SELECT : NOTIFY_CLICK);
      return 0;
    }
    break;
  case WM_MOUSEMOVE:
    if (!m_trackingMouse) {
      TRACKMOUSEEVENT track;
      track.cbSize = sizeof(track);
      track.dwFlags = TME_LEAVE;
      track.hwndTrack = m_hwnd;
      track.dwHoverTime = 0;
      m_trackingMouse = !!TrackMouseEvent(&track);
    }
    setHover(hitTest(lParam));
    return 0;
  case WM_MOUSELEAVE:
    m_trackingMouse = false;
    setHover(-1);
    return 0;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
  {
    int item = hitTest(lParam);
    if (item >= 0) {
      m_pressed = item;
      SetCapture(m_hwnd);
      invalidate();
    }
    return 0;
  }
  case WM_LBUTTONUP:
  {
    int pressed = m_pressed;
    if (pressed < 0) {
      return 0;
    }
    m_pressed = -1;
    ReleaseCapture();
    invalidate();
    if (hitTest(lParam) == pressed) {
      if (m_mode == MODE_LIST) {
        // The focus moves to the list only for the keyboard: a click on a
        // section leaves it where it is.
        select(pressed);
        notify(NOTIFY_SELECT);
      } else {
        notify(NOTIFY_CLICK);
      }
    }
    return 0;
  }
  case WM_CAPTURECHANGED:
    if (m_pressed >= 0) {
      m_pressed = -1;
      invalidate();
    }
    return 0;
  }
  return DefWindowProc(m_hwnd, message, wParam, lParam);
}

D2D1_RECT_F SettingsNav::itemRect(size_t index) const
{
  RECT client;
  GetClientRect(m_hwnd, &client);
  float width = m_chrome.pixelsToDips(client.right - client.left);
  float top = index * itemPitch();
  return D2D1::RectF(0.0f, top, width, top + kItemHeight);
}

int SettingsNav::hitTest(LPARAM lParam) const
{
  float x = m_chrome.pixelsToDips(static_cast<short>(LOWORD(lParam)));
  float y = m_chrome.pixelsToDips(static_cast<short>(HIWORD(lParam)));
  for (size_t i = 0; i < m_items.size(); i++) {
    D2D1_RECT_F rect = itemRect(i);
    if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void SettingsNav::setHover(int item)
{
  if (item != m_hover) {
    m_hover = item;
    invalidate();
  }
}

void SettingsNav::moveSelection(int index)
{
  if (m_mode != MODE_LIST || m_items.empty()) {
    return;
  }
  if (index < 0) {
    index = 0;
  }
  if (index >= static_cast<int>(m_items.size())) {
    index = static_cast<int>(m_items.size()) - 1;
  }
  if (static_cast<size_t>(index) == m_selected) {
    return;
  }
  select(index);
  notify(NOTIFY_SELECT);
}

void SettingsNav::notify(UINT code)
{
  HWND parent = GetParent(m_hwnd);
  if (parent != 0) {
    SendMessage(parent, WM_COMMAND, MAKEWPARAM(m_id, code), reinterpret_cast<LPARAM>(m_hwnd));
  }
}

void SettingsNav::invalidate()
{
  if (m_hwnd != 0) {
    InvalidateRect(m_hwnd, 0, FALSE);
  }
}

void SettingsNav::paint()
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(m_hwnd, &ps);
  if (dc == 0) {
    return;
  }
  const ThemeTokens &theme = currentTheme();
  if (!m_chrome.beginDraw()) {
    HBRUSH brush = CreateSolidBrush(RGB((theme.surface >> 16) & 0xFF,
                                        (theme.surface >> 8) & 0xFF, theme.surface & 0xFF));
    FillRect(dc, &ps.rcPaint, brush);
    DeleteObject(brush);
    EndPaint(m_hwnd, &ps);
    return;
  }
  m_chrome.clear(theme.surface);
  bool focused = GetFocus() == m_hwnd;
  for (size_t i = 0; i < m_items.size(); i++) {
    D2D1_RECT_F rect = itemRect(i);
    int item = static_cast<int>(i);
    bool selected = m_mode == MODE_LIST && i == m_selected;
    if (m_pressed == item) {
      m_chrome.fillRoundRect(rect, theme.cornerRadius, theme.pressed);
    } else if (selected) {
      m_chrome.fillRoundRect(rect, theme.cornerRadius, theme.background);
    } else if (m_hover == item) {
      m_chrome.fillRoundRect(rect, theme.cornerRadius, theme.hover);
    }
    if (selected) {
      float centerY = (rect.top + rect.bottom) / 2.0f;
      m_chrome.fillRoundRect(D2D1::RectF(rect.left, centerY - kIndicatorHeight / 2.0f,
                                         rect.left + kIndicatorWidth,
                                         centerY + kIndicatorHeight / 2.0f),
                             kIndicatorWidth / 2.0f, theme.accent);
    }
    float x = rect.left + kItemPadding;
    if (m_items[i].glyph != 0) {
      D2D1_RECT_F icon = D2D1::RectF(x, rect.top, x + kGlyphBox, rect.bottom);
      m_chrome.drawIconGlyph(m_items[i].glyph, icon, selected ? theme.accent : theme.text,
                             kGlyphBox - 2.0f);
      x = icon.right + kItemPadding;
    }
    m_chrome.drawText(m_items[i].label.c_str(), D2D1::RectF(x, rect.top, rect.right - kItemPadding,
                                                            rect.bottom),
                      theme.text, theme.fontSizeBody,
                      selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    bool focusItem = m_mode == MODE_LIST ? selected : true;
    if (focused && focusItem) {
      m_chrome.drawRoundRect(inflate(rect, -1.0f), theme.cornerRadius - 1.0f, theme.accent, 2.0f);
    }
  }
  m_chrome.endDraw();
  EndPaint(m_hwnd, &ps);
}
