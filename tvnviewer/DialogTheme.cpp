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
const UINT_PTR GROUP_BOX_SUBCLASS_ID = 0x46534742; // "FSGB"
const UINT_PTR TAB_SUBCLASS_ID = 0x46535442; // "FSTB"
const UINT_PTR LABEL_SUBCLASS_ID = 0x4653534C; // "FSSL"
const UINT_PTR HEADER_SUBCLASS_ID = 0x46534844; // "FSHD"

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

bool isGroupBox(HWND window)
{
  return hasClass(window, L"BUTTON") &&
         (GetWindowLongPtr(window, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX;
}

// A native group box draws its caption in a color that is unreadable on the
// dark background and ignores WM_CTLCOLORSTATIC when themed, so it is drawn
// here: a one pixel frame in the border color, with the caption in the text
// color in a gap of the top edge. Nothing else is painted, so the controls
// inside and the dialog background stay as they are.
void paintGroupBox(HWND box)
{
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(box, &paint);
  if (dc == 0) {
    return;
  }
  const ThemeTokens &tokens = currentTheme();
  RECT client;
  GetClientRect(box, &client);
  HFONT font = reinterpret_cast<HFONT>(SendMessage(box, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font != 0 ? SelectObject(dc, font) : 0;

  wchar_t caption[256] = { 0 };
  GetWindowTextW(box, caption, sizeof(caption) / sizeof(caption[0]));
  const int length = static_cast<int>(wcslen(caption));
  SIZE extent = { 0, 0 };
  GetTextExtentPoint32W(dc, caption, length, &extent);

  const int inset = extent.cy * 2 / 3;
  const int gap = extent.cy / 4;
  RECT frame = client;
  frame.top = extent.cy / 2;
  RECT text = { inset, 0, inset + extent.cx, extent.cy };
  RECT hole = text;
  if (length > 0) {
    InflateRect(&hole, gap, 0);
    ExcludeClipRect(dc, hole.left, hole.top, hole.right, hole.bottom);
  }
  HBRUSH border = CreateSolidBrush(toColor(tokens.border));
  FrameRect(dc, &frame, border);
  DeleteObject(border);
  if (length > 0) {
    SelectClipRgn(dc, 0);
    HBRUSH background = CreateSolidBrush(toColor(tokens.background));
    FillRect(dc, &hole, background);
    DeleteObject(background);
    const bool enabled = IsWindowEnabled(box) != FALSE;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, toColor(enabled ? tokens.text : tokens.disabledText));
    DWORD flags = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP;
    if ((SendMessage(box, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) != 0) {
      flags |= DT_HIDEPREFIX;
    }
    DrawTextW(dc, caption, length, &text, flags);
  }
  if (oldFont != 0) {
    SelectObject(dc, oldFont);
  }
  EndPaint(box, &paint);
}

// The visual style of a tab control has no dark variant (and the "Explorer"
// theme of the other controls draws no tabs at all), so the strip is drawn
// here: the captions on the dialog background, the selected one in the text
// color with an accent bar under it, the others muted, and a one pixel frame
// around the body that the page of the tab covers.
void paintTabControl(HWND tab)
{
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(tab, &paint);
  if (dc == 0) {
    return;
  }
  const ThemeTokens &tokens = currentTheme();
  RECT client;
  GetClientRect(tab, &client);
  HBRUSH background = CreateSolidBrush(toColor(tokens.background));
  FillRect(dc, &client, background);
  DeleteObject(background);

  HFONT font = reinterpret_cast<HFONT>(SendMessage(tab, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font != 0 ? SelectObject(dc, font) : 0;
  SetBkMode(dc, TRANSPARENT);

  const int count = static_cast<int>(SendMessage(tab, TCM_GETITEMCOUNT, 0, 0));
  const int selected = static_cast<int>(SendMessage(tab, TCM_GETCURSEL, 0, 0));
  const int focused = static_cast<int>(SendMessage(tab, TCM_GETCURFOCUS, 0, 0));
  const bool showFocus = GetFocus() == tab &&
                         (SendMessage(tab, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0;
  int stripBottom = 0;
  for (int i = 0; i < count; i++) {
    RECT item;
    if (SendMessage(tab, TCM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&item)) &&
        item.bottom > stripBottom) {
      stripBottom = item.bottom;
    }
  }

  HBRUSH border = CreateSolidBrush(toColor(tokens.border));
  RECT body = { client.left, stripBottom, client.right, client.bottom };
  FrameRect(dc, &body, border);
  DeleteObject(border);

  HBRUSH accent = CreateSolidBrush(toColor(tokens.accent));
  for (int i = 0; i < count; i++) {
    RECT item;
    wchar_t caption[128] = { 0 };
    TCITEMW data;
    ZeroMemory(&data, sizeof(data));
    data.mask = TCIF_TEXT;
    data.pszText = caption;
    data.cchTextMax = sizeof(caption) / sizeof(caption[0]);
    if (!SendMessage(tab, TCM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&item)) ||
        !SendMessage(tab, TCM_GETITEMW, i, reinterpret_cast<LPARAM>(&data))) {
      continue;
    }
    item.bottom = stripBottom;
    SetTextColor(dc, toColor(i == selected ? tokens.text : tokens.mutedText));
    RECT text = item;
    DrawTextW(dc, caption, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (i == selected) {
      RECT bar = { item.left, stripBottom - 2, item.right, stripBottom };
      FillRect(dc, &bar, accent);
    }
    if (showFocus && i == focused) {
      RECT focus = item;
      InflateRect(&focus, -3, -3);
      DrawFocusRect(dc, &focus);
    }
  }
  DeleteObject(accent);
  if (oldFont != 0) {
    SelectObject(dc, oldFont);
  }
  EndPaint(tab, &paint);
}

LRESULT CALLBACK tabControlProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR)
{
  switch (message) {
  case WM_PAINT:
    paintTabControl(hwnd);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_NCDESTROY:
    RemoveWindowSubclass(hwnd, tabControlProc, TAB_SUBCLASS_ID);
    break;
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}

// The header of a list view is drawn by the visual style, which paints its
// captions in a dark gray in the dark theme too. In the dark theme it is drawn
// here: the surface color of the palette, the captions in the text color and a
// divider on the right edge of each item.
void paintListHeader(HWND header)
{
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(header, &paint);
  if (dc == 0) {
    return;
  }
  const ThemeTokens &tokens = currentTheme();
  RECT client;
  GetClientRect(header, &client);
  HBRUSH surface = CreateSolidBrush(toColor(tokens.surface));
  FillRect(dc, &client, surface);
  DeleteObject(surface);

  HFONT font = reinterpret_cast<HFONT>(SendMessage(header, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font != 0 ? SelectObject(dc, font) : 0;
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, toColor(tokens.text));
  HBRUSH border = CreateSolidBrush(toColor(tokens.border));
  const int count = static_cast<int>(SendMessage(header, HDM_GETITEMCOUNT, 0, 0));
  for (int i = 0; i < count; i++) {
    RECT rect;
    wchar_t caption[128] = { 0 };
    HDITEMW item;
    ZeroMemory(&item, sizeof(item));
    item.mask = HDI_TEXT | HDI_FORMAT;
    item.pszText = caption;
    item.cchTextMax = sizeof(caption) / sizeof(caption[0]);
    if (!SendMessage(header, HDM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&rect)) ||
        !SendMessage(header, HDM_GETITEMW, i, reinterpret_cast<LPARAM>(&item))) {
      continue;
    }
    RECT divider = { rect.right - 1, rect.top, rect.right, rect.bottom };
    FillRect(dc, &divider, border);
    RECT text = rect;
    InflateRect(&text, -6, 0);
    DWORD flags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    const int align = item.fmt & HDF_JUSTIFYMASK;
    flags |= align == HDF_RIGHT ? DT_RIGHT : (align == HDF_CENTER ? DT_CENTER : DT_LEFT);
    DrawTextW(dc, caption, -1, &text, flags);
  }
  DeleteObject(border);
  if (oldFont != 0) {
    SelectObject(dc, oldFont);
  }
  EndPaint(header, &paint);
}

LRESULT CALLBACK listHeaderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR)
{
  switch (message) {
  case WM_PAINT:
    if (Theme::isDark()) {
      paintListHeader(hwnd);
      return 0;
    }
    break;
  case WM_ERASEBKGND:
    if (Theme::isDark()) {
      return 1;
    }
    break;
  case WM_NCDESTROY:
    RemoveWindowSubclass(hwnd, listHeaderProc, HEADER_SUBCLASS_ID);
    break;
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}

bool isTextLabel(HWND window)
{
  if (!hasClass(window, L"STATIC")) {
    return false;
  }
  const LONG_PTR type = GetWindowLongPtr(window, GWL_STYLE) & SS_TYPEMASK;
  return type == SS_LEFT || type == SS_CENTER || type == SS_RIGHT ||
         type == SS_SIMPLE || type == SS_LEFTNOWORDWRAP;
}

// A disabled label is drawn by the system with an embossed highlight that
// shows as a halo on the dark background. It is drawn here in the disabled
// text color instead, on the background the parent chooses.
void paintDisabledLabel(HWND label)
{
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(label, &paint);
  if (dc == 0) {
    return;
  }
  RECT client;
  GetClientRect(label, &client);
  HBRUSH background = reinterpret_cast<HBRUSH>(
    SendMessage(GetParent(label), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc),
                reinterpret_cast<LPARAM>(label)));
  if (background != 0) {
    FillRect(dc, &client, background);
  }
  HFONT font = reinterpret_cast<HFONT>(SendMessage(label, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font != 0 ? SelectObject(dc, font) : 0;
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, toColor(currentTheme().disabledText));

  const LONG_PTR style = GetWindowLongPtr(label, GWL_STYLE);
  const LONG_PTR type = style & SS_TYPEMASK;
  DWORD flags = DT_TOP;
  flags |= type == SS_CENTER ? DT_CENTER : (type == SS_RIGHT ? DT_RIGHT : DT_LEFT);
  flags |= (type == SS_SIMPLE || type == SS_LEFTNOWORDWRAP) ? DT_SINGLELINE : DT_WORDBREAK;
  if ((style & SS_NOPREFIX) != 0) {
    flags |= DT_NOPREFIX;
  }
  if ((SendMessage(label, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) != 0) {
    flags |= DT_HIDEPREFIX;
  }
  wchar_t caption[512] = { 0 };
  GetWindowTextW(label, caption, sizeof(caption) / sizeof(caption[0]));
  DrawTextW(dc, caption, -1, &client, flags);
  if (oldFont != 0) {
    SelectObject(dc, oldFont);
  }
  EndPaint(label, &paint);
}

LRESULT CALLBACK labelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                           UINT_PTR, DWORD_PTR)
{
  switch (message) {
  case WM_PAINT:
    if (!IsWindowEnabled(hwnd)) {
      paintDisabledLabel(hwnd);
      return 0;
    }
    break;
  case WM_ENABLE:
    DefSubclassProc(hwnd, message, wParam, lParam);
    InvalidateRect(hwnd, 0, TRUE);
    return 0;
  case WM_NCDESTROY:
    RemoveWindowSubclass(hwnd, labelProc, LABEL_SUBCLASS_ID);
    break;
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK groupBoxProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                              UINT_PTR, DWORD_PTR)
{
  switch (message) {
  case WM_PAINT:
    paintGroupBox(hwnd);
    return 0;
  case WM_ERASEBKGND:
    // Transparent: the dialog has painted what is behind the frame.
    return 1;
  case WM_SETTEXT:
  case WM_ENABLE:
  case WM_SETFONT: {
    LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    // The old caption is not erased by this window: the parent repaints.
    RECT rect;
    GetWindowRect(hwnd, &rect);
    HWND parent = GetParent(hwnd);
    MapWindowPoints(0, parent, reinterpret_cast<POINT *>(&rect), 2);
    InvalidateRect(parent, &rect, TRUE);
    return result;
  }
  case WM_NCDESTROY:
    RemoveWindowSubclass(hwnd, groupBoxProc, GROUP_BOX_SUBCLASS_ID);
    break;
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
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

void DialogTheme::themeGroupBox(HWND box)
{
  SetWindowSubclass(box, groupBoxProc, GROUP_BOX_SUBCLASS_ID, 0);
}

void DialogTheme::themeTabControl(HWND tab)
{
  SetWindowSubclass(tab, tabControlProc, TAB_SUBCLASS_ID, 0);
}

// A list view takes the palette for its rows (its scroll bars come from the
// "DarkMode_Explorer" theme every control gets) and a header drawn here in the
// dark theme.
void DialogTheme::themeListView(HWND list)
{
  const ThemeTokens &tokens = currentTheme();
  HWND header = ListView_GetHeader(list);
  if (header != 0) {
    SetWindowSubclass(header, listHeaderProc, HEADER_SUBCLASS_ID, 0);
    InvalidateRect(header, 0, TRUE);
  }
  ListView_SetBkColor(list, toColor(tokens.field));
  ListView_SetTextBkColor(list, toColor(tokens.field));
  ListView_SetTextColor(list, toColor(tokens.text));
}

void DialogTheme::themeLabel(HWND label)
{
  SetWindowSubclass(label, labelProc, LABEL_SUBCLASS_ID, 0);
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
    } else if (isGroupBox(child)) {
      themeGroupBox(child);
    } else if (hasClass(child, WC_TABCONTROLW)) {
      themeTabControl(child);
    } else if (hasClass(child, WC_LISTVIEWW)) {
      themeListView(child);
    } else if (isTextLabel(child)) {
      themeLabel(child);
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
