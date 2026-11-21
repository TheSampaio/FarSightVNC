// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "DialogTheme.h"

#include "gui/DpiUtils.h"
#include "gui/d2d/Theme.h"

#include <commctrl.h>
#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

namespace {

const UINT_PTR SUBCLASS_ID = 0x46534454; // "FSDT"

COLORREF toColor(ThemeColor color)
{
  return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

bool hasClass(HWND window, const wchar_t *name)
{
  wchar_t className[32] = { 0 };
  GetClassNameW(window, className, sizeof(className) / sizeof(className[0]));
  return _wcsicmp(className, name) == 0;
}

void controlRect(HWND parent, HWND control, RECT *rect)
{
  GetWindowRect(control, rect);
  MapWindowPoints(0, parent, reinterpret_cast<POINT *>(rect), 2);
}

}

DialogTheme::DialogTheme()
: m_hwnd(0),
  m_backgroundBrush(0),
  m_fieldBrush(0),
  m_headingFont(0)
{
}

DialogTheme::~DialogTheme()
{
  detach();
  deleteBrushes();
  if (m_headingFont != 0) {
    DeleteObject(m_headingFont);
  }
}

void DialogTheme::attach(HWND dialog)
{
  detach();
  m_hwnd = dialog;
  if (m_hwnd == 0) {
    return;
  }
  SetWindowSubclass(m_hwnd, subclassProc, SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this));
  updateFonts();
  apply();
}

void DialogTheme::detach()
{
  if (m_hwnd != 0) {
    RemoveWindowSubclass(m_hwnd, subclassProc, SUBCLASS_ID);
    m_hwnd = 0;
  }
}

// A dialog that is created again (a modeless one reopened) may register its
// controls again: each id is kept once.
void DialogTheme::setHeading(UINT controlId)
{
  addId(m_headings, controlId);
  if (m_hwnd != 0) {
    updateFonts();
  }
}

void DialogTheme::setMuted(UINT controlId)
{
  addId(m_muted, controlId);
}

void DialogTheme::addBorder(UINT controlId)
{
  addId(m_borders, controlId);
}

void DialogTheme::addId(std::vector<UINT> &ids, UINT controlId)
{
  for (size_t i = 0; i < ids.size(); i++) {
    if (ids[i] == controlId) {
      return;
    }
  }
  ids.push_back(controlId);
}

void DialogTheme::setOnApplied(const std::function<void()> &callback)
{
  m_onApplied = callback;
}

void DialogTheme::setOnDpiChanged(const std::function<void()> &callback)
{
  m_onDpiChanged = callback;
}

bool DialogTheme::contains(const std::vector<UINT> &ids, HWND control) const
{
  const UINT id = static_cast<UINT>(GetDlgCtrlID(control));
  for (size_t i = 0; i < ids.size(); i++) {
    if (ids[i] == id) {
      return true;
    }
  }
  return false;
}

void DialogTheme::deleteBrushes()
{
  if (m_backgroundBrush != 0) {
    DeleteObject(m_backgroundBrush);
    m_backgroundBrush = 0;
  }
  if (m_fieldBrush != 0) {
    DeleteObject(m_fieldBrush);
    m_fieldBrush = 0;
  }
}

// The heading font is derived from the font of the dialog, which the dialog
// manager created for the DPI of the monitor: it follows the DPI with it.
void DialogTheme::updateFonts()
{
  if (m_hwnd == 0 || m_headings.empty()) {
    return;
  }
  HFONT base = reinterpret_cast<HFONT>(SendMessage(m_hwnd, WM_GETFONT, 0, 0));
  LOGFONTW logFont;
  if (base == 0 || GetObjectW(base, sizeof(logFont), &logFont) != sizeof(logFont)) {
    return;
  }
  logFont.lfHeight = MulDiv(logFont.lfHeight, 4, 3);
  logFont.lfWeight = FW_SEMIBOLD;
  HFONT font = CreateFontIndirectW(&logFont);
  if (font == 0) {
    return;
  }
  for (size_t i = 0; i < m_headings.size(); i++) {
    SendDlgItemMessage(m_hwnd, m_headings[i], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  }
  if (m_headingFont != 0) {
    DeleteObject(m_headingFont);
  }
  m_headingFont = font;
}

// A themed progress bar has no dark variant and ignores the colors, so it is
// drawn unthemed: a flat bar in the accent color on the surface color, with
// the border of the dialog around it instead of the classic one.
void DialogTheme::themeProgressBar(HWND bar)
{
  const ThemeTokens &tokens = currentTheme();
  SetWindowTheme(bar, L"", L"");
  LONG_PTR style = GetWindowLongPtr(bar, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtr(bar, GWL_EXSTYLE);
  LONG_PTR newStyle = (style | PBS_SMOOTH) & ~static_cast<LONG_PTR>(WS_BORDER);
  LONG_PTR newExStyle = exStyle & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
  if (newStyle != style || newExStyle != exStyle) {
    SetWindowLongPtr(bar, GWL_STYLE, newStyle);
    SetWindowLongPtr(bar, GWL_EXSTYLE, newExStyle);
    SetWindowPos(bar, 0, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  SendMessage(bar, PBM_SETBARCOLOR, 0, toColor(tokens.accent));
  SendMessage(bar, PBM_SETBKCOLOR, 0, toColor(tokens.surface));
}

void DialogTheme::apply()
{
  if (m_hwnd == 0) {
    return;
  }
  const ThemeTokens &tokens = currentTheme();
  deleteBrushes();
  m_backgroundBrush = CreateSolidBrush(toColor(tokens.background));
  m_fieldBrush = CreateSolidBrush(toColor(tokens.field));
  if ((GetWindowLongPtr(m_hwnd, GWL_STYLE) & WS_CHILD) == 0) {
    Theme::applyWindowDarkMode(m_hwnd);
  }
  Theme::applyControlThemeTree(m_hwnd);
  for (HWND child = GetWindow(m_hwnd, GW_CHILD); child != 0;
       child = GetWindow(child, GW_HWNDNEXT)) {
    if (hasClass(child, PROGRESS_CLASSW)) {
      themeProgressBar(child);
    } else if (hasClass(child, L"#32770")) {
      // A child dialog (a tab page) has its own DialogTheme, and the theme
      // broadcast only reaches the top-level windows.
      SendMessage(child, Theme::changedMessage(), 0, 0);
    }
  }
  if (m_onApplied) {
    m_onApplied();
  }
  RedrawWindow(m_hwnd, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void DialogTheme::paintBorders()
{
  HDC dc = GetDC(m_hwnd);
  if (dc == 0) {
    return;
  }
  HBRUSH border = CreateSolidBrush(toColor(currentTheme().border));
  for (HWND child = GetWindow(m_hwnd, GW_CHILD); child != 0;
       child = GetWindow(child, GW_HWNDNEXT)) {
    if (!IsWindowVisible(child) ||
        (!hasClass(child, PROGRESS_CLASSW) && !contains(m_borders, child))) {
      continue;
    }
    RECT rect;
    controlRect(m_hwnd, child, &rect);
    InflateRect(&rect, 1, 1);
    FrameRect(dc, &rect, border);
  }
  DeleteObject(border);
  ReleaseDC(m_hwnd, dc);
}

LRESULT CALLBACK DialogTheme::subclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR, DWORD_PTR data)
{
  return reinterpret_cast<DialogTheme *>(data)->onMessage(hwnd, message, wParam, lParam);
}

LRESULT DialogTheme::onMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  // Not a constant (RegisterWindowMessage), so it cannot be a case label.
  if (message == Theme::changedMessage()) {
    apply();
    return 0;
  }
  const ThemeTokens &tokens = currentTheme();
  switch (message) {
  case WM_CTLCOLORDLG:
    return reinterpret_cast<LRESULT>(m_backgroundBrush);
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX: {
    HWND control = reinterpret_cast<HWND>(lParam);
    // A read-only or disabled EDIT sends WM_CTLCOLORSTATIC: still a field.
    const bool field = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ||
                       hasClass(control, L"EDIT");
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, toColor(contains(m_muted, control) ? tokens.mutedText : tokens.text));
    SetBkColor(dc, toColor(field ? tokens.field : tokens.background));
    return reinterpret_cast<LRESULT>(field ? m_fieldBrush : m_backgroundBrush);
  }
  case WM_NOTIFY: {
    // Check boxes and radio buttons are drawn by Theme in the dark theme
    // (readable labels).
    const NMHDR *header = reinterpret_cast<const NMHDR *>(lParam);
    if (header != 0 && header->code == NM_CUSTOMDRAW &&
        Theme::customDrawButton(header->hwndFrom, reinterpret_cast<void *>(lParam))) {
      return CDRF_SKIPDEFAULT;
    }
    break;
  }
  case WM_DPICHANGED: {
    // The dialog manager scales the layout and the dialog font first.
    LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    updateFonts();
    if (m_onDpiChanged) {
      m_onDpiChanged();
    }
    return result;
  }
  case WM_PAINT: {
    LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    paintBorders();
    return result;
  }
  case WM_NCDESTROY:
    detach();
    break;
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}
