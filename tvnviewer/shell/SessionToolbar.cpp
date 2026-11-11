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

#include "gui/DpiUtils.h"
#include "../resource.h"

namespace {

const wchar_t kWindowClassName[] = L"FsvSessionToolbar";

enum ActionGroup
{
  GROUP_DISPLAY,
  GROUP_INPUT,
  GROUP_TRANSFER,
  GROUP_SESSION,
  GROUP_OVERFLOW
};

enum ActionKind
{
  KIND_BUTTON,    // Posts its command.
  KIND_TOGGLE,    // Posts its command; drawn checked when the state says so.
  KIND_MENU,      // Opens a drop-down menu (SessionToolbarListener).
  KIND_OVERFLOW   // A drop-down menu without the chevron ("...").
};

// One action of the bar. glyph is a codepoint of the system icon font
// (Segoe Fluent Icons / Segoe MDL2 Assets); 0 draws label as text. label is
// also the fallback when no icon font is installed. The icon phase replaces
// the glyphs of this table with Lucide icons.
struct ActionInfo
{
  SessionAction action;
  ActionGroup group;
  ActionKind kind;
  int command;
  wchar_t glyph;
  const wchar_t *label;
  float width;            // DIPs, chevron of a drop-down not included.
  const wchar_t *tooltip;
};

const ActionInfo kActions[SESSION_ACTION_COUNT] = {
  { SESSION_ACTION_FIT_WINDOW, GROUP_DISPLAY, KIND_TOGGLE, IDS_TB_SCALEAUTO, 0xE9A6, L"Fit", 28.0f,
    L"Fit to window" },
  { SESSION_ACTION_SCALE_100, GROUP_DISPLAY, KIND_TOGGLE, IDS_TB_SCALE100, 0, L"1:1", 30.0f,
    L"Original size (100%)" },
  { SESSION_ACTION_ZOOM_IN, GROUP_DISPLAY, KIND_BUTTON, IDS_TB_SCALEIN, 0xE8A3, L"+", 28.0f,
    L"Zoom in" },
  { SESSION_ACTION_ZOOM_OUT, GROUP_DISPLAY, KIND_BUTTON, IDS_TB_SCALEOUT, 0xE71F, L"-", 28.0f,
    L"Zoom out" },
  { SESSION_ACTION_FULL_SCREEN, GROUP_DISPLAY, KIND_TOGGLE, IDS_TB_FULLSCREEN, 0xE740, L"[ ]", 28.0f,
    L"Full screen (Ctrl+Alt+Shift+F)" },
  { SESSION_ACTION_MONITOR, GROUP_DISPLAY, KIND_MENU, 0, 0xE7F4, L"Mon", 28.0f,
    L"Full screen on a monitor" },

  { SESSION_ACTION_CTRL_ALT_DEL, GROUP_INPUT, KIND_BUTTON, IDS_TB_CTRLALTDEL, 0xE785, L"CAD", 28.0f,
    L"Send Ctrl+Alt+Del (Ctrl+Alt+Shift+Del)" },
  { SESSION_ACTION_WIN_KEY, GROUP_INPUT, KIND_BUTTON, ID_SEND_WIN_KEY, 0, L"Win", 34.0f,
    L"Send the Win key" },
  { SESSION_ACTION_CTRL_LATCH, GROUP_INPUT, KIND_TOGGLE, IDS_TB_CTRL, 0, L"Ctrl", 36.0f,
    L"Ctrl key down" },
  { SESSION_ACTION_ALT_LATCH, GROUP_INPUT, KIND_TOGGLE, IDS_TB_ALT, 0, L"Alt", 32.0f,
    L"Alt key down" },
  { SESSION_ACTION_SPECIAL_KEYS, GROUP_INPUT, KIND_MENU, 0, 0xE765, L"Keys", 28.0f,
    L"Special keys" },

  { SESSION_ACTION_FILE_TRANSFER, GROUP_TRANSFER, KIND_BUTTON, IDS_TB_TRANSFER, 0xE8B7, L"Files", 28.0f,
    L"Transfer files (Ctrl+Alt+Shift+E)" },

  { SESSION_ACTION_PAUSE, GROUP_SESSION, KIND_TOGGLE, IDS_TB_PAUSE, 0xE769, L"||", 28.0f,
    L"Pause screen updates" },
  { SESSION_ACTION_REFRESH, GROUP_SESSION, KIND_BUTTON, IDS_TB_REFRESH, 0xE72C, L"R", 28.0f,
    L"Request screen refresh (Ctrl+Alt+Shift+R)" },

  { SESSION_ACTION_MORE, GROUP_OVERFLOW, KIND_OVERFLOW, 0, 0xE712, L"...", 28.0f,
    L"More" },
};

// Other glyphs of the bar.
const wchar_t kGlyphChevronDown = 0xE70D;
const wchar_t kGlyphLock = 0xE72E;

// Sizes in DIPs.
const float kButtonGap = 2.0f;
const float kGroupGap = 6.0f;
const float kDividerHeight = 16.0f;
const float kChevronWidth = 12.0f;
const float kChevronSize = 8.0f;
const float kLockSize = 14.0f;
const float kLatencyWidth = 44.0f;

bool contains(const D2D1_RECT_F &rect, float x, float y)
{
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

bool isEmpty(const D2D1_RECT_F &rect)
{
  return rect.right <= rect.left;
}

}

SessionToolbar::SessionToolbar()
: m_hwnd(0),
  m_tooltip(0),
  m_focusTarget(0),
  m_listener(0),
  m_latency(D2D1::RectF()),
  m_lock(D2D1::RectF()),
  m_showConnection(false),
  m_hover(-1),
  m_pressed(-1),
  m_trackingMouse(false),
  m_alive(new bool(true))
{
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    m_buttons[i] = D2D1::RectF();
  }
  m_state.enabled.set();
}

SessionToolbar::~SessionToolbar()
{
  *m_alive = false;
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
  createTooltips();
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
    toolbar->m_tooltip = 0;
    toolbar->m_hover = -1;
    toolbar->m_pressed = -1;
    toolbar->m_trackingMouse = false;
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
  case WM_MOUSEMOVE:
    trackMouseLeave();
    setHover(hitTest(lParam));
    return 0;
  case WM_MOUSELEAVE:
    m_trackingMouse = false;
    if (m_pressed < 0) {
      setHover(-1);
    }
    return 0;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
    onButtonDown(hitTest(lParam));
    return 0;
  case WM_LBUTTONUP:
    onButtonUp(hitTest(lParam));
    return 0;
  case WM_CAPTURECHANGED:
    if (m_pressed >= 0 && reinterpret_cast<HWND>(lParam) != m_hwnd) {
      m_pressed = -1;
      invalidate();
    }
    return 0;
  }
  return DefWindowProc(m_hwnd, message, wParam, lParam);
}

int SessionToolbar::getHeight() const
{
  return m_chrome.dipsToPixels(kLightTheme.toolbarHeight);
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
  if (state == m_state) {
    return;
  }
  m_state = state;
  invalidate();
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
  const ThemeTokens &theme = kLightTheme;
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

  m_dividers.clear();
  float x = theme.spacing;
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    const ActionInfo &info = kActions[i];
    if (i > 0 && info.group != kActions[i - 1].group) {
      // A hairline divider between groups.
      float middle = x - kButtonGap + kGroupGap;
      float dividerTop = (height - kDividerHeight) / 2.0f;
      m_dividers.push_back(D2D1::RectF(middle, dividerTop, middle + 1.0f,
                                       dividerTop + kDividerHeight));
      x = middle + 1.0f + kGroupGap;
    }
    float w = info.width + (info.kind == KIND_MENU ? kChevronWidth : 0.0f);
    // A button that does not fit is not shown (the overflow menu and the
    // system menu still have every command).
    if (x + w > width - theme.spacing) {
      m_buttons[i] = D2D1::RectF(x, top, x, bottom);
    } else {
      m_buttons[i] = D2D1::RectF(x, top, x + w, bottom);
    }
    x += w + kButtonGap;
  }
  // Drop the dividers past the last visible button.
  while (!m_dividers.empty() && m_dividers.back().left > width - theme.spacing) {
    m_dividers.pop_back();
  }
  m_showConnection = x + theme.spacingLarge <= m_latency.left;
  updateTooltipRects();
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
    const ThemeTokens &theme = kLightTheme;
    if (m_chrome.beginDraw()) {
      RECT client;
      GetClientRect(m_hwnd, &client);
      float width = m_chrome.pixelsToDips(client.right - client.left);
      float height = m_chrome.pixelsToDips(client.bottom - client.top);
      m_chrome.clear(theme.background);
      // Hairline between the bar and the remote desktop.
      float line = m_chrome.pixelsToDips(1);
      m_chrome.fillRect(D2D1::RectF(0.0f, height - line, width, height), theme.border);

      for (size_t i = 0; i < m_dividers.size(); i++) {
        D2D1_RECT_F divider = m_dividers[i];
        divider.right = divider.left + line;
        m_chrome.fillRect(divider, theme.border);
      }
      for (size_t i = 0; i < SESSION_ACTION_COUNT; i++) {
        paintButton(i);
      }
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

void SessionToolbar::paintButton(size_t index)
{
  const ThemeTokens &theme = kLightTheme;
  const ActionInfo &info = kActions[index];
  const D2D1_RECT_F &rect = m_buttons[index];
  if (isEmpty(rect)) {
    return;
  }
  bool enabled = m_state.enabled.test(index);
  bool checked = m_state.checked.test(index);
  int i = static_cast<int>(index);
  // A pressed button shows its pressed color only while the pointer is
  // still over it, like the native buttons.
  bool pressed = enabled && m_pressed == i && m_hover == i;
  bool hover = enabled && m_hover == i && (m_pressed < 0 || pressed);

  ThemeColor glyph = enabled ? theme.text : theme.disabledText;
  bool hasBackground = true;
  ThemeColor background = theme.background;
  if (pressed) {
    background = checked ? theme.selectedPressed : theme.pressed;
  } else if (checked) {
    background = hover ? theme.selectedHover : theme.selected;
  } else if (hover) {
    background = theme.hover;
  } else {
    hasBackground = false;
  }
  if (checked && enabled) {
    glyph = theme.selectedText;
  }
  if (hasBackground) {
    m_chrome.fillRoundRect(rect, theme.cornerRadius, background);
  }

  D2D1_RECT_F icon = rect;
  if (info.kind == KIND_MENU) {
    icon.right -= kChevronWidth;
    D2D1_RECT_F chevron = D2D1::RectF(icon.right - 2.0f, rect.top, rect.right - 2.0f, rect.bottom);
    if (!m_chrome.drawIconGlyph(kGlyphChevronDown, chevron, glyph, kChevronSize)) {
      m_chrome.drawText(L"\x25BE", chevron, glyph, theme.fontSizeSmall,
                        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
  }
  if (info.glyph == 0 || !m_chrome.drawIconGlyph(info.glyph, icon, glyph, theme.glyphSize)) {
    m_chrome.drawText(info.label, icon, glyph, theme.fontSizeSmall,
                      DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
  }
}

void SessionToolbar::createTooltips()
{
  m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, 0,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              m_hwnd, 0, GetModuleHandle(0), 0);
  if (m_tooltip == 0) {
    return;
  }
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    TOOLINFOW tool;
    ZeroMemory(&tool, sizeof(tool));
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_SUBCLASS;
    tool.hwnd = m_hwnd;
    tool.uId = static_cast<UINT_PTR>(i);
    tool.lpszText = const_cast<wchar_t *>(kActions[i].tooltip);
    SendMessage(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}

void SessionToolbar::updateTooltipRects()
{
  if (m_tooltip == 0) {
    return;
  }
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    TOOLINFOW tool;
    ZeroMemory(&tool, sizeof(tool));
    tool.cbSize = sizeof(tool);
    tool.hwnd = m_hwnd;
    tool.uId = static_cast<UINT_PTR>(i);
    const D2D1_RECT_F &rect = m_buttons[i];
    tool.rect.left = m_chrome.dipsToPixels(rect.left);
    tool.rect.top = m_chrome.dipsToPixels(rect.top);
    tool.rect.right = m_chrome.dipsToPixels(rect.right);
    tool.rect.bottom = m_chrome.dipsToPixels(rect.bottom);
    SendMessage(m_tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}

int SessionToolbar::hitTest(LPARAM lParam) const
{
  float x = m_chrome.pixelsToDips(static_cast<short>(LOWORD(lParam)));
  float y = m_chrome.pixelsToDips(static_cast<short>(HIWORD(lParam)));
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    if (!isEmpty(m_buttons[i]) && contains(m_buttons[i], x, y)) {
      return i;
    }
  }
  return -1;
}

void SessionToolbar::setHover(int index)
{
  if (index != m_hover) {
    m_hover = index;
    invalidate();
  }
}

void SessionToolbar::trackMouseLeave()
{
  if (m_trackingMouse) {
    return;
  }
  TRACKMOUSEEVENT track;
  track.cbSize = sizeof(track);
  track.dwFlags = TME_LEAVE;
  track.hwndTrack = m_hwnd;
  track.dwHoverTime = 0;
  m_trackingMouse = !!TrackMouseEvent(&track);
}

void SessionToolbar::onButtonDown(int index)
{
  if (index < 0 || !m_state.enabled.test(index)) {
    return;
  }
  m_pressed = index;
  m_hover = index;
  if (kActions[index].kind != KIND_MENU && kActions[index].kind != KIND_OVERFLOW) {
    SetCapture(m_hwnd);
    invalidate();
    return;
  }
  // A drop-down opens on the press and stays pressed while its menu is
  // open.
  RedrawWindow(m_hwnd, 0, 0, RDW_INVALIDATE | RDW_UPDATENOW);
  const D2D1_RECT_F &button = m_buttons[index];
  RECT anchor = { m_chrome.dipsToPixels(button.left), m_chrome.dipsToPixels(button.top),
                  m_chrome.dipsToPixels(button.right), m_chrome.dipsToPixels(button.bottom) };
  MapWindowPoints(m_hwnd, 0, reinterpret_cast<POINT *>(&anchor), 2);
  std::shared_ptr<bool> alive = m_alive;
  HWND hwnd = m_hwnd;
  if (m_listener != 0) {
    m_listener->onToolbarMenu(kActions[index].action, anchor);
  }
  if (!*alive || !IsWindow(hwnd)) {
    return;
  }
  m_pressed = -1;
  POINT cursor;
  if (GetCursorPos(&cursor) && ScreenToClient(m_hwnd, &cursor)) {
    m_hover = hitTest(MAKELPARAM(cursor.x, cursor.y));
  } else {
    m_hover = -1;
  }
  m_trackingMouse = false;
  trackMouseLeave();
  invalidate();
  restoreFocus();
}

void SessionToolbar::onButtonUp(int index)
{
  if (m_pressed < 0) {
    return;
  }
  int pressed = m_pressed;
  m_pressed = -1;
  if (GetCapture() == m_hwnd) {
    ReleaseCapture();
  }
  invalidate();
  if (index == pressed && m_state.enabled.test(index)) {
    // Posted like the commands of a toolbar control: the command may open
    // a modal dialog or end the session, never inside this call.
    PostMessage(GetParent(m_hwnd), WM_COMMAND, MAKEWPARAM(kActions[index].command, 0),
                reinterpret_cast<LPARAM>(m_hwnd));
    restoreFocus();
  }
}

void SessionToolbar::restoreFocus()
{
  // Only while the frame is active: a click on the bar of an inactive frame
  // does not activate it.
  if (m_focusTarget != 0 && IsWindow(m_focusTarget) &&
      GetForegroundWindow() == GetAncestor(m_hwnd, GA_ROOT) && GetFocus() != m_focusTarget) {
    SetFocus(m_focusTarget);
  }
}
