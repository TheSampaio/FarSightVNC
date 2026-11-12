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

#include "SettingsForm.h"

#include "gui/DpiUtils.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>

namespace {

const wchar_t kWindowClassName[] = L"FsvSettingsForm";

// Layout in DIPs, on an 8 DIP grid.
const float kMarginX = 40.0f;
const float kMaxWidth = 640.0f;
const float kTop = 32.0f;
const float kTitleSize = 24.0f;
const float kTitleHeight = 32.0f;
const float kHeadingGap = 8.0f;
const float kRowsGap = 16.0f;
const float kHeaderSize = 16.0f;
const float kHeaderGap = 24.0f;
const float kHeaderHeight = 32.0f;
const float kNoteSize = 13.0f;
const float kNoteGap = 8.0f;
const float kControlFontSize = 14.0f;
const float kCheckRow = 32.0f;
const float kCheckHeight = 24.0f;
const float kCheckBox = 32.0f;     // Box and gap before the text.
const float kFieldRow = 40.0f;
const float kFieldHeight = 28.0f;
const float kLabelWidth = 216.0f;
const float kNumberWidth = 96.0f;
const float kTrackWidth = 200.0f;
const float kTrackHeight = 32.0f;
const float kCaptionWidth = 40.0f;
const float kButtonHeight = 32.0f;
const float kButtonPadding = 16.0f;
const float kButtonMinWidth = 96.0f;
const float kGap = 8.0f;
const float kIndent = 24.0f;
const float kBottom = 40.0f;
const float kWheelStep = 48.0f;
const float kComboDropHeight = 240.0f;
const float kScrollbarWidth = 4.0f;

COLORREF toColorRef(ThemeColor color)
{
  return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

}

SettingsForm::SettingsForm(HWND parent, SettingsFormListener *listener)
: m_hwnd(0),
  m_listener(listener),
  m_font(0),
  m_background(CreateSolidBrush(toColorRef(kLightTheme.background))),
  m_subtitleHeight(0.0f),
  m_contentHeight(0.0f),
  m_scroll(0.0f)
{
  if (!registerClass()) {
    return;
  }
  // A control parent: the dialog manager of the page reaches the controls.
  CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClassName, L"",
                  WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                  0, 0, 0, 0, parent, 0, GetModuleHandle(0), this);
  if (m_hwnd == 0) {
    return;
  }
  m_chrome.attach(m_hwnd, DpiUtils::getDpiForWindow(m_hwnd));
  updateFont();
}

SettingsForm::~SettingsForm()
{
  if (m_hwnd != 0) {
    DestroyWindow(m_hwnd);
  }
  if (m_font != 0) {
    DeleteObject(m_font);
  }
  if (m_background != 0) {
    DeleteObject(m_background);
  }
}

bool SettingsForm::registerClass()
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

void SettingsForm::updateFont()
{
  HFONT font = CreateFontW(-m_chrome.dipsToPixels(kControlFontSize), 0, 0, 0, FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           kLightTheme.fontFamily);
  for (HWND child = GetWindow(m_hwnd, GW_CHILD); child != 0; child = GetWindow(child, GW_HWNDNEXT)) {
    SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
  }
  if (m_font != 0) {
    DeleteObject(m_font);
  }
  m_font = font;
}

void SettingsForm::setDpi(UINT dpi)
{
  if (dpi == 0 || dpi == m_chrome.getDpi()) {
    return;
  }
  m_chrome.setDpi(dpi);
  updateFont();
  layout();
}

void SettingsForm::clear()
{
  // The controls go first: their notifications must not reach old rows.
  HWND child;
  while ((child = GetWindow(m_hwnd, GW_CHILD)) != 0) {
    DestroyWindow(child);
  }
  m_rows.clear();
  m_title.clear();
  m_subtitle.clear();
  m_scroll = 0.0f;
  m_contentHeight = 0.0f;
  invalidate();
}

void SettingsForm::setHeading(const wchar_t *title, const wchar_t *subtitle)
{
  m_title = title != 0 ? title : L"";
  m_subtitle = subtitle != 0 ? subtitle : L"";
}

LRESULT CALLBACK SettingsForm::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  SettingsForm *form;
  if (message == WM_NCCREATE) {
    CREATESTRUCT *create = reinterpret_cast<CREATESTRUCT *>(lParam);
    form = static_cast<SettingsForm *>(create->lpCreateParams);
    form->m_hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(form));
  } else {
    form = reinterpret_cast<SettingsForm *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (form == 0) {
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    form->m_hwnd = 0;
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  return form->onMessage(message, wParam, lParam);
}

LRESULT CALLBACK SettingsForm::controlProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR id, DWORD_PTR data)
{
  SettingsForm *form = reinterpret_cast<SettingsForm *>(data);
  switch (message) {
  case WM_SETFOCUS:
    // Tab may reach a control below the visible part of the form.
    form->ensureVisible(hwnd);
    break;
  case WM_MOUSEWHEEL:
    // The wheel scrolls the form instead of changing the value under the
    // pointer (combo boxes, track bars, spin buttons).
    if (form->m_hwnd != 0) {
      return SendMessage(form->m_hwnd, message, wParam, lParam);
    }
    break;
  case WM_NCDESTROY:
    RemoveWindowSubclass(hwnd, controlProc, id);
    break;
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT SettingsForm::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
  case WM_SIZE:
    m_chrome.resize(LOWORD(lParam), HIWORD(lParam));
    layout();
    return 0;
  case WM_ERASEBKGND:
  {
    // The themed check boxes and radio buttons ask their parent for the
    // background behind them (DrawThemeParentBackground) with this message
    // and WM_PRINTCLIENT. The page itself is painted by WM_PAINT.
    RECT box;
    HDC dc = reinterpret_cast<HDC>(wParam);
    if (GetClipBox(dc, &box) != ERROR) {
      FillRect(dc, &box, m_background);
    }
    return 1;
  }
  case WM_PRINTCLIENT:
  {
    RECT client;
    GetClientRect(m_hwnd, &client);
    FillRect(reinterpret_cast<HDC>(wParam), &client, m_background);
    return 0;
  }
  case WM_PAINT:
    paint();
    return 0;
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORBTN:
  {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, toColorRef(kLightTheme.text));
    SetBkColor(dc, toColorRef(kLightTheme.background));
    return reinterpret_cast<LRESULT>(m_background);
  }
  case WM_COMMAND:
    if (lParam != 0 && m_listener != 0) {
      m_listener->onFormCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
      return 0;
    }
    break;
  case WM_HSCROLL:
    if (lParam != 0 && m_listener != 0) {
      m_listener->onFormHScroll(reinterpret_cast<HWND>(lParam));
      return 0;
    }
    break;
  case WM_MOUSEWHEEL:
  {
    float notches = static_cast<short>(HIWORD(wParam)) / static_cast<float>(WHEEL_DELTA);
    scrollTo(m_scroll - notches * kWheelStep);
    return 0;
  }
  }
  return DefWindowProc(m_hwnd, message, wParam, lParam);
}

HWND SettingsForm::createControl(const wchar_t *className, const wchar_t *text, DWORD style,
                                 UINT id)
{
  HWND hwnd = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                              0, 0, 0, 0, m_hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              GetModuleHandle(0), 0);
  if (hwnd == 0) {
    return 0;
  }
  SendMessage(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), FALSE);
  SetWindowSubclass(hwnd, controlProc, 1, reinterpret_cast<DWORD_PTR>(this));
  return hwnd;
}

SettingsForm::Row SettingsForm::newRow(RowKind kind, const wchar_t *text, int indent)
{
  Row row;
  row.kind = kind;
  row.text = text != 0 ? text : L"";
  row.indent = indent;
  row.width = 0.0f;
  row.label = 0;
  row.control = 0;
  row.extra = 0;
  row.value = 0;
  row.trailing = 0;
  row.top = 0.0f;
  row.height = 0.0f;
  return row;
}

void SettingsForm::header(const wchar_t *text)
{
  m_rows.push_back(newRow(ROW_HEADER, text, 0));
}

void SettingsForm::note(const wchar_t *text, int indent)
{
  m_rows.push_back(newRow(ROW_NOTE, text, indent));
}

void SettingsForm::value(const wchar_t *label, const wchar_t *text)
{
  Row row = newRow(ROW_VALUE, label, 0);
  row.detail = text != 0 ? text : L"";
  m_rows.push_back(row);
}

HWND SettingsForm::checkBox(UINT id, const wchar_t *label, int indent)
{
  Row row = newRow(ROW_CHECK, label, indent);
  row.control = createControl(L"BUTTON", label, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, id);
  m_rows.push_back(row);
  return row.control;
}

HWND SettingsForm::radio(UINT id, const wchar_t *label, bool group, int indent)
{
  Row row = newRow(ROW_CHECK, label, indent);
  row.control = createControl(L"BUTTON", label,
                              BS_AUTORADIOBUTTON | (group ? WS_GROUP | WS_TABSTOP : 0), id);
  m_rows.push_back(row);
  return row.control;
}

HWND SettingsForm::comboBox(UINT id, const wchar_t *label, float width)
{
  Row row = newRow(ROW_FIELD, label, 0);
  row.width = width;
  row.label = createControl(L"STATIC", label, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
  row.control = createControl(L"COMBOBOX", L"",
                              CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, id);
  m_rows.push_back(row);
  return row.control;
}

HWND SettingsForm::numberField(UINT id, const wchar_t *label, int minimum, int maximum)
{
  Row row = newRow(ROW_FIELD, label, 0);
  row.width = kNumberWidth;
  row.label = createControl(L"STATIC", label, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
  row.control = createControl(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER |
                                           WS_TABSTOP | WS_GROUP, id);
  if (row.control != 0) {
    SendMessage(row.control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(m_chrome.dipsToPixels(4.0f), m_chrome.dipsToPixels(4.0f)));
    row.extra = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT |
                                UDS_ARROWKEYS | UDS_NOTHOUSANDS | UDS_HOTTRACK,
                                0, 0, 0, 0, m_hwnd, 0, GetModuleHandle(0), 0);
    if (row.extra != 0) {
      SetWindowSubclass(row.extra, controlProc, 1, reinterpret_cast<DWORD_PTR>(this));
      SendMessage(row.extra, UDM_SETBUDDY, reinterpret_cast<WPARAM>(row.control), 0);
      SendMessage(row.extra, UDM_SETRANGE32, minimum, maximum);
    }
  }
  m_rows.push_back(row);
  return row.control;
}

HWND SettingsForm::textField(UINT id, const wchar_t *label)
{
  Row row = newRow(ROW_FIELD, label, 0);
  row.label = createControl(L"STATIC", label, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
  row.control = createControl(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_BORDER |
                                           WS_TABSTOP | WS_GROUP, id);
  if (row.control != 0) {
    SendMessage(row.control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(m_chrome.dipsToPixels(4.0f), m_chrome.dipsToPixels(4.0f)));
  }
  m_rows.push_back(row);
  return row.control;
}

HWND SettingsForm::trackBar(UINT id, UINT lowId, const wchar_t *low, UINT highId,
                            const wchar_t *high, UINT valueId, int indent)
{
  Row row = newRow(ROW_TRACK, L"", indent);
  row.label = createControl(L"STATIC", low, SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX, lowId);
  row.control = createControl(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP | WS_GROUP,
                              id);
  row.extra = createControl(L"STATIC", high, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, highId);
  row.value = createControl(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, valueId);
  m_rows.push_back(row);
  return row.control;
}

HWND SettingsForm::button(UINT id, const wchar_t *label, bool sameRow)
{
  HWND hwnd = createControl(L"BUTTON", label, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP, id);
  if (sameRow && !m_rows.empty() && m_rows.back().trailing == 0 &&
      (m_rows.back().kind == ROW_FIELD || m_rows.back().kind == ROW_BUTTON)) {
    m_rows.back().trailing = hwnd;
    return hwnd;
  }
  Row row = newRow(ROW_BUTTON, label, 0);
  row.control = hwnd;
  m_rows.push_back(row);
  return hwnd;
}

HWND SettingsForm::control(UINT id) const
{
  return id != 0 && m_hwnd != 0 ? GetDlgItem(m_hwnd, id) : 0;
}

float SettingsForm::textWidth(const wchar_t *text) const
{
  // GDI and DirectWrite widths differ slightly: a little room is added.
  float width = const_cast<ChromeRenderer &>(m_chrome).measureText(text, kControlFontSize);
  return std::ceil(width) + kGap;
}

float SettingsForm::buttonWidthOf(HWND button) const
{
  wchar_t text[64] = L"";
  GetWindowTextW(button, text, 64);
  return (std::max)(kButtonMinWidth, textWidth(text) + 2.0f * kButtonPadding);
}

float SettingsForm::contentLeft() const
{
  return kMarginX;
}

float SettingsForm::contentWidth() const
{
  RECT client;
  GetClientRect(m_hwnd, &client);
  float width = m_chrome.pixelsToDips(client.right - client.left) - 2.0f * kMarginX;
  return (std::max)(240.0f, (std::min)(kMaxWidth, width));
}

void SettingsForm::layout()
{
  if (m_hwnd == 0) {
    return;
  }
  float width = contentWidth();
  float y = kTop + kTitleHeight + kHeadingGap;
  m_subtitleHeight = 0.0f;
  if (!m_subtitle.empty()) {
    m_subtitleHeight = std::ceil(m_chrome.measureParagraph(m_subtitle.c_str(), width,
                                                           m_chrome.pixelsToDips(0) + 14.0f));
    y += m_subtitleHeight;
  }
  y += kRowsGap;
  for (size_t i = 0; i < m_rows.size(); i++) {
    Row &row = m_rows[i];
    switch (row.kind) {
    case ROW_HEADER:
      if (i > 0) {
        y += kHeaderGap - kGap;
      }
      row.height = kHeaderHeight;
      break;
    case ROW_NOTE:
    {
      float noteWidth = width - row.indent * kIndent;
      float text = std::ceil(m_chrome.measureParagraph(row.text.c_str(), noteWidth, kNoteSize));
      row.height = text + kNoteGap;
      break;
    }
    case ROW_VALUE:
    case ROW_CHECK:
      row.height = kCheckRow;
      break;
    case ROW_FIELD:
    case ROW_TRACK:
    case ROW_BUTTON:
      row.height = kFieldRow;
      break;
    }
    row.top = y;
    y += row.height;
  }
  m_contentHeight = y + kBottom;
  placeRows();
  // Keeps the scroll offset inside the new content.
  float offset = m_scroll;
  m_scroll = -1.0f;
  scrollTo(offset);
}

void SettingsForm::place(HWND hwnd, float left, float top, float width, float height)
{
  if (hwnd == 0) {
    return;
  }
  int x = m_chrome.dipsToPixels(left);
  int y = m_chrome.dipsToPixels(top - m_scroll);
  SetWindowPos(hwnd, 0, x, y, (std::max)(m_chrome.dipsToPixels(left + width) - x, 0),
               (std::max)(m_chrome.dipsToPixels(top - m_scroll + height) - y, 0),
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsForm::placeRows()
{
  float left = contentLeft();
  float width = contentWidth();
  for (size_t i = 0; i < m_rows.size(); i++) {
    Row &row = m_rows[i];
    float x = left + row.indent * kIndent;
    float available = left + width - x;
    float center = row.top + row.height / 2.0f;
    switch (row.kind) {
    case ROW_CHECK:
      place(row.control, x, center - kCheckHeight / 2.0f,
            (std::min)(available, kCheckBox + textWidth(row.text.c_str())), kCheckHeight);
      break;
    case ROW_FIELD:
    {
      place(row.label, x, center - kFieldHeight / 2.0f, kLabelWidth - kGap, kFieldHeight);
      float controlLeft = x + kLabelWidth;
      float controlWidth = row.width > 0.0f ? row.width : available - kLabelWidth;
      wchar_t className[16] = L"";
      GetClassNameW(row.control, className, 16);
      bool combo = _wcsicmp(className, L"COMBOBOX") == 0;
      HWND button = row.trailing;
      HWND spin = row.extra;
      float buttonWidth = button != 0 ? buttonWidthOf(button) : 0.0f;
      if (button != 0 && row.width <= 0.0f) {
        controlWidth -= buttonWidth + kGap;
      }
      if (combo) {
        // The closed height of a combo box follows its font: center it.
        place(row.control, controlLeft, row.top, controlWidth, kComboDropHeight);
        RECT rect;
        GetWindowRect(row.control, &rect);
        float closed = m_chrome.pixelsToDips(rect.bottom - rect.top);
        place(row.control, controlLeft, center - closed / 2.0f, controlWidth, kComboDropHeight);
      } else {
        place(row.control, controlLeft, center - kFieldHeight / 2.0f, controlWidth, kFieldHeight);
      }
      if (spin != 0) {
        // The spin buttons attach to the right side of the field again.
        SendMessage(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(row.control), 0);
      }
      if (button != 0) {
        place(button, controlLeft + controlWidth + kGap, center - kButtonHeight / 2.0f,
              buttonWidth, kButtonHeight);
      }
      break;
    }
    case ROW_TRACK:
      place(row.label, x, center - kFieldHeight / 2.0f, kCaptionWidth, kFieldHeight);
      x += kCaptionWidth + kGap;
      place(row.control, x, center - kTrackHeight / 2.0f, kTrackWidth, kTrackHeight);
      x += kTrackWidth + kGap;
      place(row.extra, x, center - kFieldHeight / 2.0f, kCaptionWidth, kFieldHeight);
      x += kCaptionWidth + kGap;
      place(row.value, x, center - kFieldHeight / 2.0f, kCaptionWidth, kFieldHeight);
      break;
    case ROW_BUTTON:
    {
      float buttonWidth = buttonWidthOf(row.control);
      place(row.control, x, center - kButtonHeight / 2.0f, buttonWidth, kButtonHeight);
      if (row.trailing != 0) {
        place(row.trailing, x + buttonWidth + kGap, center - kButtonHeight / 2.0f,
              buttonWidthOf(row.trailing), kButtonHeight);
      }
      break;
    }
    default:
      break;
    }
  }
}

void SettingsForm::scrollTo(float offset)
{
  if (m_hwnd == 0) {
    return;
  }
  RECT client;
  GetClientRect(m_hwnd, &client);
  float view = m_chrome.pixelsToDips(client.bottom - client.top);
  float maximum = (std::max)(0.0f, m_contentHeight - view);
  offset = std::floor((std::max)(0.0f, (std::min)(offset, maximum)));
  if (offset == m_scroll) {
    return;
  }
  m_scroll = offset;
  placeRows();
  invalidate();
}

void SettingsForm::ensureVisible(HWND control)
{
  for (size_t i = 0; i < m_rows.size(); i++) {
    const Row &row = m_rows[i];
    if (row.control != control && row.extra != control && row.label != control &&
        row.trailing != control) {
      continue;
    }
    RECT client;
    GetClientRect(m_hwnd, &client);
    float view = m_chrome.pixelsToDips(client.bottom - client.top);
    float margin = kLightTheme.spacingLarge;
    if (row.top - margin < m_scroll) {
      scrollTo(row.top - margin);
    } else if (row.top + row.height + margin > m_scroll + view) {
      scrollTo(row.top + row.height + margin - view);
    }
    return;
  }
}

void SettingsForm::invalidate()
{
  if (m_hwnd != 0) {
    InvalidateRect(m_hwnd, 0, TRUE);
  }
}

void SettingsForm::paint()
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(m_hwnd, &ps);
  if (dc == 0) {
    return;
  }
  const ThemeTokens &theme = kLightTheme;
  if (!m_chrome.beginDraw()) {
    FillRect(dc, &ps.rcPaint, m_background);
    EndPaint(m_hwnd, &ps);
    return;
  }
  m_chrome.clear(theme.background);
  float left = contentLeft();
  float width = contentWidth();
  float top = kTop - m_scroll;
  m_chrome.drawText(m_title.c_str(), D2D1::RectF(left, top, left + width, top + kTitleHeight),
                    theme.text, kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  if (!m_subtitle.empty()) {
    float y = top + kTitleHeight + kHeadingGap;
    m_chrome.drawParagraph(m_subtitle.c_str(), D2D1::RectF(left, y, left + width, y + m_subtitleHeight),
                           theme.mutedText, 14.0f);
  }
  for (size_t i = 0; i < m_rows.size(); i++) {
    const Row &row = m_rows[i];
    float x = left + row.indent * kIndent;
    float y = row.top - m_scroll;
    switch (row.kind) {
    case ROW_HEADER:
      m_chrome.drawText(row.text.c_str(), D2D1::RectF(x, y, left + width, y + row.height),
                        theme.text, kHeaderSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
      break;
    case ROW_NOTE:
      m_chrome.drawParagraph(row.text.c_str(), D2D1::RectF(x, y, left + width, y + row.height),
                             theme.mutedText, kNoteSize);
      break;
    case ROW_VALUE:
      m_chrome.drawText(row.text.c_str(), D2D1::RectF(x, y, x + kLabelWidth - kGap, y + row.height),
                        theme.mutedText, kControlFontSize);
      m_chrome.drawText(row.detail.c_str(), D2D1::RectF(x + kLabelWidth, y, left + width, y + row.height),
                        theme.text, kControlFontSize);
      break;
    default:
      break;
    }
  }
  // A thin scroll indicator when the rows do not fit.
  RECT client;
  GetClientRect(m_hwnd, &client);
  float view = m_chrome.pixelsToDips(client.bottom - client.top);
  float viewWidth = m_chrome.pixelsToDips(client.right - client.left);
  if (m_contentHeight > view && view > 0.0f) {
    float thumb = (std::max)(32.0f, view * view / m_contentHeight);
    float range = view - thumb - 2.0f * kGap;
    float position = kGap + range * m_scroll / (m_contentHeight - view);
    m_chrome.fillRoundRect(D2D1::RectF(viewWidth - kScrollbarWidth - kGap / 2.0f, position,
                                       viewWidth - kGap / 2.0f, position + thumb),
                           kScrollbarWidth / 2.0f, theme.border);
  }
  m_chrome.endDraw();
  EndPaint(m_hwnd, &ps);
}
