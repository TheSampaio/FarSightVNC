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

#include "SessionActionStrip.h"

#include "../resource.h"

#include <CommCtrl.h>

namespace {

enum ActionGroup
{
  GROUP_DISPLAY,
  GROUP_INPUT,
  GROUP_TRANSFER,
  GROUP_SESSION,
  GROUP_OVERFLOW,
  GROUP_WINDOW
};

enum ActionKind
{
  KIND_BUTTON,    // Posts its command.
  KIND_TOGGLE,    // Posts its command; drawn checked when the state says so.
  KIND_MENU,      // Opens a drop-down menu (SessionToolbarListener).
  KIND_OVERFLOW,  // A drop-down menu without the chevron ("...").
  KIND_DANGER     // Posts its command; red when hovered (disconnect).
};

const int PLACE_BAR = SESSION_PLACEMENT_BAR;
const int PLACE_PILL = SESSION_PLACEMENT_PILL;
const int PLACE_BOTH = SESSION_PLACEMENT_BAR | SESSION_PLACEMENT_PILL;

// One action of the strips. glyph is a codepoint of the system icon font
// (Segoe Fluent Icons / Segoe MDL2 Assets); 0 draws label as text. label is
// also the fallback when no icon font is installed. The icon phase replaces
// the glyphs of this table with Lucide icons.
struct ActionInfo
{
  SessionAction action;
  ActionGroup group;
  ActionKind kind;
  int placement;
  int command;
  wchar_t glyph;
  const wchar_t *label;
  float width;            // DIPs, chevron of a drop-down not included.
  const wchar_t *tooltip;
};

const ActionInfo kActions[SESSION_ACTION_COUNT] = {
  { SESSION_ACTION_FULL_SCREEN, GROUP_DISPLAY, KIND_TOGGLE, PLACE_BAR, IDS_TB_FULLSCREEN, 0xE740,
    L"[ ]", 28.0f, L"Full screen (Ctrl+Alt+Shift+F)" },
  { SESSION_ACTION_MONITOR, GROUP_DISPLAY, KIND_MENU, PLACE_BOTH, 0, 0xE7F4,
    L"Mon", 28.0f, L"Full screen on a monitor" },

  { SESSION_ACTION_CTRL_ALT_DEL, GROUP_INPUT, KIND_BUTTON, PLACE_BOTH, IDS_TB_CTRLALTDEL, 0xE785,
    L"CAD", 28.0f, L"Send Ctrl+Alt+Del (Ctrl+Alt+Shift+Del)" },
  { SESSION_ACTION_WIN_KEY, GROUP_INPUT, KIND_BUTTON, PLACE_BOTH, ID_SEND_WIN_KEY, 0,
    L"Win", 34.0f, L"Send the Win key" },
  { SESSION_ACTION_CTRL_LATCH, GROUP_INPUT, KIND_TOGGLE, PLACE_BOTH, IDS_TB_CTRL, 0,
    L"Ctrl", 36.0f, L"Ctrl key down" },
  { SESSION_ACTION_ALT_LATCH, GROUP_INPUT, KIND_TOGGLE, PLACE_BOTH, IDS_TB_ALT, 0,
    L"Alt", 32.0f, L"Alt key down" },
  { SESSION_ACTION_SPECIAL_KEYS, GROUP_INPUT, KIND_MENU, PLACE_BOTH, 0, 0xE765,
    L"Keys", 28.0f, L"Special keys" },

  { SESSION_ACTION_FILE_TRANSFER, GROUP_TRANSFER, KIND_BUTTON, PLACE_BOTH, IDS_TB_TRANSFER, 0xE8B7,
    L"Files", 28.0f, L"Transfer files (Ctrl+Alt+Shift+E)" },

  { SESSION_ACTION_PAUSE, GROUP_SESSION, KIND_TOGGLE, PLACE_BOTH, IDS_TB_PAUSE, 0xE769,
    L"||", 28.0f, L"Pause screen updates" },
  { SESSION_ACTION_REFRESH, GROUP_SESSION, KIND_BUTTON, PLACE_BOTH, IDS_TB_REFRESH, 0xE72C,
    L"R", 28.0f, L"Request screen refresh (Ctrl+Alt+Shift+R)" },

  { SESSION_ACTION_MORE, GROUP_OVERFLOW, KIND_OVERFLOW, PLACE_BOTH, 0, 0xE712,
    L"...", 28.0f, L"More" },

  { SESSION_ACTION_PIN, GROUP_WINDOW, KIND_TOGGLE, PLACE_PILL, ID_PILL_PIN, 0xE718,
    L"Pin", 28.0f, L"Keep the toolbar visible" },
  { SESSION_ACTION_EXIT_FULL_SCREEN, GROUP_WINDOW, KIND_BUTTON, PLACE_PILL, IDS_TB_FULLSCREEN, 0xE73F,
    L"Exit", 28.0f, L"Leave full screen (Ctrl+Alt+Shift+F)" },
  { SESSION_ACTION_DISCONNECT, GROUP_WINDOW, KIND_DANGER, PLACE_PILL, ID_PILL_DISCONNECT, 0xE711,
    L"X", 28.0f, L"Disconnect" },
};

// Other glyphs.
const wchar_t kGlyphChevronDown = 0xE70D;

// Sizes in DIPs.
const float kButtonGap = 2.0f;
const float kGroupGap = 6.0f;
const float kDividerHeight = 16.0f;
const float kChevronWidth = 12.0f;
const float kChevronSize = 8.0f;

bool contains(const D2D1_RECT_F &rect, float x, float y)
{
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

bool isEmpty(const D2D1_RECT_F &rect)
{
  return rect.right <= rect.left;
}

// Places the buttons of placement from left (see SessionActionStrip::layout).
// buttons and dividers may be 0 to measure only. Returns the right edge of the
// last button that fits.
float placeButtons(int placement, float left, float right, float top, float bottom,
                   D2D1_RECT_F *buttons, std::vector<D2D1_RECT_F> *dividers)
{
  const ThemeTokens &theme = kLightTheme;
  float buttonTop = top + (bottom - top - theme.buttonSize) / 2.0f;
  float buttonBottom = buttonTop + theme.buttonSize;
  float dividerTop = top + (bottom - top - kDividerHeight) / 2.0f;
  float x = left;
  float end = left;
  int previous = -1;
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    const ActionInfo &info = kActions[i];
    if ((info.placement & placement) == 0) {
      if (buttons != 0) {
        buttons[i] = D2D1::RectF();
      }
      continue;
    }
    if (previous >= 0 && info.group != kActions[previous].group) {
      // A hairline divider between groups, kept only before a shown button.
      float middle = x - kButtonGap + kGroupGap;
      if (dividers != 0 && middle < right) {
        dividers->push_back(D2D1::RectF(middle, dividerTop, middle + 1.0f,
                                        dividerTop + kDividerHeight));
      }
      x = middle + 1.0f + kGroupGap;
    }
    previous = i;
    float w = info.width + (info.kind == KIND_MENU ? kChevronWidth : 0.0f);
    bool fits = x + w <= right;
    if (buttons != 0) {
      buttons[i] = fits ? D2D1::RectF(x, buttonTop, x + w, buttonBottom)
                        : D2D1::RectF(x, buttonTop, x, buttonBottom);
    }
    if (fits) {
      end = x + w;
    }
    x += w + kButtonGap;
  }
  // Drop the dividers past the last shown button.
  while (dividers != 0 && !dividers->empty() && dividers->back().left > end) {
    dividers->pop_back();
  }
  return end;
}

}

SessionActionStrip::SessionActionStrip(SessionActionPlacement placement)
: m_placement(placement),
  m_hwnd(0),
  m_tooltip(0),
  m_commandTarget(0),
  m_focusTarget(0),
  m_chrome(0),
  m_listener(0),
  m_hover(-1),
  m_pressed(-1),
  m_trackingMouse(false),
  m_menuOpen(false),
  m_alive(new bool(true))
{
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    m_buttons[i] = D2D1::RectF();
  }
  m_state.enabled.set();
}

SessionActionStrip::~SessionActionStrip()
{
  *m_alive = false;
}

void SessionActionStrip::attach(HWND window, ChromeRenderer *chrome, HWND commandTarget,
                                SessionToolbarListener *listener,
                                std::function<void()> invalidate)
{
  m_hwnd = window;
  m_chrome = chrome;
  m_commandTarget = commandTarget;
  m_listener = listener;
  m_invalidate = invalidate;
  createTooltips();
}

void SessionActionStrip::detach()
{
  // The tooltip window is owned by the window and destroyed with it.
  m_hwnd = 0;
  m_tooltip = 0;
  m_hover = -1;
  m_pressed = -1;
  m_trackingMouse = false;
}

bool SessionActionStrip::isPlaced(int index) const
{
  return (kActions[index].placement & m_placement) != 0;
}

bool SessionActionStrip::setState(const SessionToolbarState &state)
{
  if (state == m_state) {
    return false;
  }
  m_state = state;
  invalidate();
  return true;
}

float SessionActionStrip::layout(float left, float right, float top, float bottom)
{
  m_dividers.clear();
  float end = placeButtons(m_placement, left, right, top, bottom, m_buttons, &m_dividers);
  updateTooltipRects();
  invalidate();
  return end;
}

float SessionActionStrip::fullWidth(SessionActionPlacement placement)
{
  return placeButtons(placement, 0.0f, 1.0e6f, 0.0f, kLightTheme.toolbarHeight, 0, 0);
}

void SessionActionStrip::invalidate()
{
  if (m_hwnd != 0 && m_invalidate) {
    m_invalidate();
  }
}

void SessionActionStrip::paint()
{
  const ThemeTokens &theme = kLightTheme;
  float line = m_chrome->pixelsToDips(1);
  for (size_t i = 0; i < m_dividers.size(); i++) {
    D2D1_RECT_F divider = m_dividers[i];
    divider.right = divider.left + line;
    m_chrome->fillRect(divider, theme.border);
  }
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    paintButton(i);
  }
}

void SessionActionStrip::paintButton(int index)
{
  const ThemeTokens &theme = kLightTheme;
  const ActionInfo &info = kActions[index];
  const D2D1_RECT_F &rect = m_buttons[index];
  if (!isPlaced(index) || isEmpty(rect)) {
    return;
  }
  bool enabled = m_state.enabled.test(index);
  bool checked = m_state.checked.test(index);
  // A pressed button shows its pressed color only while the pointer is
  // still over it, like the native buttons.
  bool pressed = enabled && m_pressed == index && m_hover == index;
  bool hover = enabled && m_hover == index && (m_pressed < 0 || pressed);
  bool danger = info.kind == KIND_DANGER;

  ThemeColor glyph = enabled ? theme.text : theme.disabledText;
  bool hasBackground = true;
  ThemeColor background = theme.background;
  if (pressed) {
    background = danger ? theme.closePressed : checked ? theme.selectedPressed : theme.pressed;
  } else if (checked) {
    background = hover ? theme.selectedHover : theme.selected;
  } else if (hover) {
    background = danger ? theme.closeHover : theme.hover;
  } else {
    hasBackground = false;
  }
  if (checked && enabled) {
    glyph = theme.selectedText;
  }
  if (danger && (hover || pressed)) {
    glyph = theme.closeHoverText;
  }
  if (hasBackground) {
    m_chrome->fillRoundRect(rect, theme.cornerRadius, background);
  }

  D2D1_RECT_F icon = rect;
  if (info.kind == KIND_MENU) {
    icon.right -= kChevronWidth;
    D2D1_RECT_F chevron = D2D1::RectF(icon.right - 2.0f, rect.top, rect.right - 2.0f, rect.bottom);
    if (!m_chrome->drawIconGlyph(kGlyphChevronDown, chevron, glyph, kChevronSize)) {
      m_chrome->drawText(L"\x25BE", chevron, glyph, theme.fontSizeSmall,
                         DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
  }
  if (info.glyph == 0 || !m_chrome->drawIconGlyph(info.glyph, icon, glyph, theme.glyphSize)) {
    m_chrome->drawText(info.label, icon, glyph, theme.fontSizeSmall,
                       DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
  }
}

void SessionActionStrip::createTooltips()
{
  m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, 0,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              m_hwnd, 0, GetModuleHandle(0), 0);
  if (m_tooltip == 0) {
    return;
  }
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    if (!isPlaced(i)) {
      continue;
    }
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

void SessionActionStrip::updateTooltipRects()
{
  if (m_tooltip == 0) {
    return;
  }
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    if (!isPlaced(i)) {
      continue;
    }
    TOOLINFOW tool;
    ZeroMemory(&tool, sizeof(tool));
    tool.cbSize = sizeof(tool);
    tool.hwnd = m_hwnd;
    tool.uId = static_cast<UINT_PTR>(i);
    const D2D1_RECT_F &rect = m_buttons[i];
    tool.rect.left = m_chrome->dipsToPixels(rect.left);
    tool.rect.top = m_chrome->dipsToPixels(rect.top);
    tool.rect.right = m_chrome->dipsToPixels(rect.right);
    tool.rect.bottom = m_chrome->dipsToPixels(rect.bottom);
    SendMessage(m_tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}

bool SessionActionStrip::onMouseMessage(UINT message, WPARAM, LPARAM lParam)
{
  switch (message) {
  case WM_MOUSEMOVE:
    trackMouseLeave();
    setHover(hitTest(lParam));
    return true;
  case WM_MOUSELEAVE:
    m_trackingMouse = false;
    if (m_pressed < 0) {
      setHover(-1);
    }
    return true;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
    onButtonDown(hitTest(lParam));
    return true;
  case WM_LBUTTONUP:
    onButtonUp(hitTest(lParam));
    return true;
  case WM_CAPTURECHANGED:
    if (m_pressed >= 0 && reinterpret_cast<HWND>(lParam) != m_hwnd) {
      m_pressed = -1;
      invalidate();
    }
    return true;
  }
  return false;
}

int SessionActionStrip::hitTest(LPARAM lParam) const
{
  if (m_chrome == 0) {
    return -1;
  }
  float x = m_chrome->pixelsToDips(static_cast<short>(LOWORD(lParam)));
  float y = m_chrome->pixelsToDips(static_cast<short>(HIWORD(lParam)));
  for (int i = 0; i < SESSION_ACTION_COUNT; i++) {
    if (isPlaced(i) && !isEmpty(m_buttons[i]) && contains(m_buttons[i], x, y)) {
      return i;
    }
  }
  return -1;
}

void SessionActionStrip::setHover(int index)
{
  if (index != m_hover) {
    m_hover = index;
    invalidate();
  }
}

void SessionActionStrip::trackMouseLeave()
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

void SessionActionStrip::onButtonDown(int index)
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
  invalidate();
  UpdateWindow(m_hwnd);
  const D2D1_RECT_F &button = m_buttons[index];
  RECT anchor = { m_chrome->dipsToPixels(button.left), m_chrome->dipsToPixels(button.top),
                  m_chrome->dipsToPixels(button.right), m_chrome->dipsToPixels(button.bottom) };
  MapWindowPoints(m_hwnd, 0, reinterpret_cast<POINT *>(&anchor), 2);
  std::shared_ptr<bool> alive = m_alive;
  HWND hwnd = m_hwnd;
  m_menuOpen = true;
  if (m_listener != 0) {
    m_listener->onToolbarMenu(kActions[index].action, anchor);
  }
  if (!*alive || !IsWindow(hwnd) || m_hwnd != hwnd) {
    return;
  }
  m_menuOpen = false;
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

void SessionActionStrip::onButtonUp(int index)
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
    PostMessage(m_commandTarget, WM_COMMAND, MAKEWPARAM(kActions[index].command, 0),
                reinterpret_cast<LPARAM>(m_hwnd));
    restoreFocus();
  }
}

void SessionActionStrip::restoreFocus()
{
  // Only while the frame of the focus target is active: a click on the bar
  // of an inactive frame does not activate it.
  if (m_focusTarget != 0 && IsWindow(m_focusTarget) &&
      GetForegroundWindow() == GetAncestor(m_focusTarget, GA_ROOT) &&
      GetFocus() != m_focusTarget) {
    SetFocus(m_focusTarget);
  }
}
