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

#include "HomePage.h"

#include "client-config-lib/ConnectionConfigSM.h"
#include "client-config-lib/ViewerConfig.h"
#include "gui/DpiUtils.h"
#include "../ConnectionData.h"
#include "../DefaultConnectionConfig.h"
#include "../NamingDefs.h"
#include "../OptionsDialog.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

const wchar_t kWindowClassName[] = L"FsvHomePage";

// Texts of the page (the viewer UI is in English).
const wchar_t kHeading[] = L"Connect to a remote computer";
const wchar_t kSubtitle[] = L"Enter the address of a computer that runs a VNC server.";
const wchar_t kPlaceholder[] = L"Host address (e.g. 192.168.0.10 or host::5901)";
const wchar_t kConnect[] = L"Connect";
const wchar_t kOptions[] = L"Connection options\x2026";
const wchar_t kListen[] = L"Listening mode";
const wchar_t kStopListening[] = L"Stop listening";
const wchar_t kListeningFormat[] = L"Listening for incoming connections on port %d";
const wchar_t kFavorites[] = L"Favorites";
const wchar_t kRecents[] = L"Recent";
const wchar_t kEmptyTitle[] = L"No saved computers yet";
const wchar_t kEmptyLine1[] = L"The computers you connect to appear here.";
const wchar_t kEmptyLine2[] = L"Right-click one to add it to your favorites.";
const wchar_t kMenuConnect[] = L"Connect";
const wchar_t kMenuAddFavorite[] = L"Add to favorites";
const wchar_t kMenuRemoveFavorite[] = L"Remove from favorites";
const wchar_t kMenuRemoveRecent[] = L"Remove from history";

// System icon font glyphs (placeholders until the Lucide icons).
const wchar_t kGlyphHost = 0xE7F4;      // TVMonitor
const wchar_t kGlyphOptions = 0xE9E9;   // Equalizer
const wchar_t kGlyphListen = 0xE701;    // Wifi
const wchar_t kGlyphFavorite = 0xE735;  // FavoriteStarFill

// Layout in DIPs.
const float kColumnWidth = 600.0f;
const float kSideMargin = 24.0f;
const float kTopMargin = 56.0f;
const float kHeadingSize = 24.0f;
const float kHeadingHeight = 34.0f;
const float kSubtitleHeight = 20.0f;
const float kFieldHeight = 44.0f;
const float kFieldRadius = 8.0f;
const float kFieldPadding = 14.0f;
const float kFieldFontSize = 15.0f;
const float kConnectWidth = 120.0f;
const float kLinkHeight = 32.0f;
const float kLinkPadding = 10.0f;
const float kLinkGlyph = 16.0f;
const float kSectionGap = 28.0f;
const float kHeaderHeight = 28.0f;
const float kRowHeight = 56.0f;
const float kRowGap = 2.0f;
const float kHostIcon = 36.0f;
const float kEmptyHeight = 150.0f;
const float kBottomMargin = 32.0f;
const float kWheelStep = 48.0f;

enum MenuCommand
{
  MENU_CONNECT = 1,
  MENU_FAVORITE,
  MENU_REMOVE_RECENT
};

COLORREF toColorRef(ThemeColor color)
{
  return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

std::wstring toWide(const TCHAR *text)
{
  if (text == 0) {
    return std::wstring();
  }
#ifdef UNICODE
  return std::wstring(text);
#else
  int length = MultiByteToWideChar(CP_ACP, 0, text, -1, 0, 0);
  if (length <= 1) {
    return std::wstring();
  }
  std::wstring result(length - 1, L'\0');
  MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], length);
  return result;
#endif
}

StringStorage toStorage(const std::wstring &text)
{
#ifdef UNICODE
  return StringStorage(text.c_str());
#else
  int length = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, 0, 0, 0, 0);
  std::vector<char> buffer(length > 0 ? length : 1, '\0');
  if (length > 0) {
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, &buffer[0], length, 0, 0);
  }
  return StringStorage(&buffer[0]);
#endif
}

// Options saved for host (by the history), over the options of new
// connections (Settings).
ConnectionConfig savedConfig(const std::wstring &host)
{
  ConnectionConfig config;
  DefaultConnectionConfig::load(&config);
  if (!host.empty()) {
    StringStorage key = toStorage(host);
    ConnectionConfigSM ccsm(RegistryPaths::VIEWER_PATH, key.getString());
    config.loadFromStorage(&ccsm);
  }
  return config;
}

D2D1_RECT_F inflate(const D2D1_RECT_F &rect, float amount)
{
  return D2D1::RectF(rect.left - amount, rect.top - amount,
                     rect.right + amount, rect.bottom + amount);
}

bool contains(const D2D1_RECT_F &rect, float x, float y)
{
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

}

HomePage::HomePage(HWND shell, HomeActions *actions)
: m_hwnd(0),
  m_shell(shell),
  m_edit(0),
  m_editBaseProc(0),
  m_editFont(0),
  m_fieldBrush(CreateSolidBrush(toColorRef(kLightTheme.background))),
  m_actions(actions),
  m_favoriteCount(0),
  m_optionsEdited(false),
  m_contentHeight(0.0f),
  m_scroll(0.0f),
  m_hover(ITEM_NONE),
  m_pressed(ITEM_NONE),
  m_focus(ITEM_ADDRESS),
  m_hostFocus(0),
  m_trackingMouse(false),
  m_connecting(false),
  m_alive(new bool(true))
{
  D2D1_RECT_F empty = D2D1::RectF();
  m_heading = m_subtitle = m_field = m_connect = m_options = m_listen = empty;
  m_listenStatus = m_favoritesHeader = m_recentsHeader = m_empty = empty;

  if (!registerClass()) {
    return;
  }
  CreateWindowExW(0, kWindowClassName, L"Home",
                  WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                  0, 0, 0, 0, shell, 0, GetModuleHandle(0), this);
  if (m_hwnd == 0) {
    return;
  }
  m_chrome.attach(m_hwnd, DpiUtils::getDpiForWindow(m_hwnd));
  createEdit();
  loadHosts();
  layout();
}

HomePage::~HomePage()
{
  *m_alive = false;
  if (m_hwnd != 0) {
    DestroyWindow(m_hwnd);
  }
  if (m_editFont != 0) {
    DeleteObject(m_editFont);
  }
  if (m_fieldBrush != 0) {
    DeleteObject(m_fieldBrush);
  }
}

bool HomePage::registerClass()
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

void HomePage::createEdit()
{
  m_edit = CreateWindowExW(0, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           0, 0, 0, 0, m_hwnd, 0, GetModuleHandle(0), 0);
  if (m_edit == 0) {
    return;
  }
  SetWindowLongPtr(m_edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  m_editBaseProc = reinterpret_cast<WNDPROC>(
    SetWindowLongPtr(m_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProc)));
  // Shown while the field is empty, focused or not (Common Controls 6).
  SendMessage(m_edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(kPlaceholder));
  updateEditFont();
}

void HomePage::updateEditFont()
{
  if (m_edit == 0) {
    return;
  }
  HFONT font = CreateFontW(-m_chrome.dipsToPixels(kFieldFontSize), 0, 0, 0, FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           kLightTheme.fontFamily);
  SendMessage(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SendMessage(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
  if (m_editFont != 0) {
    DeleteObject(m_editFont);
  }
  m_editFont = font;
}

LRESULT CALLBACK HomePage::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  HomePage *page;
  if (message == WM_NCCREATE) {
    CREATESTRUCT *create = reinterpret_cast<CREATESTRUCT *>(lParam);
    page = static_cast<HomePage *>(create->lpCreateParams);
    page->m_hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(page));
  } else {
    page = reinterpret_cast<HomePage *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (page == 0) {
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    page->m_hwnd = 0;
    page->m_edit = 0;
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  return page->onMessage(message, wParam, lParam);
}

LRESULT CALLBACK HomePage::editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  HomePage *page = reinterpret_cast<HomePage *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  switch (message) {
  case WM_KEYDOWN:
    if (page->onKey(wParam, true)) {
      return 0;
    }
    break;
  case WM_CHAR:
    // Tab, Enter and Ctrl+N are handled on WM_KEYDOWN: no beep.
    if (wParam == L'\t' || wParam == L'\r' || wParam == L'\n' || wParam == 0x0E) {
      return 0;
    }
    break;
  }
  return CallWindowProc(page->m_editBaseProc, hwnd, message, wParam, lParam);
}

LRESULT HomePage::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
  case WM_SIZE:
    m_chrome.resize(LOWORD(lParam), HIWORD(lParam));
    layout();
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint();
    return 0;
  case WM_CTLCOLOREDIT:
  {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, toColorRef(kLightTheme.text));
    SetBkColor(dc, toColorRef(kLightTheme.background));
    return reinterpret_cast<LRESULT>(m_fieldBrush);
  }
  case WM_COMMAND:
    if (reinterpret_cast<HWND>(lParam) == m_edit && m_edit != 0) {
      switch (HIWORD(wParam)) {
      case EN_CHANGE:
        // The Connect button follows the field (enabled when not empty).
        invalidate();
        break;
      case EN_SETFOCUS:
        m_focus = ITEM_ADDRESS;
        invalidate();
        break;
      case EN_KILLFOCUS:
        invalidate();
        break;
      }
      return 0;
    }
    break;
  case WM_GETDLGCODE:
    return DLGC_WANTALLKEYS;
  case WM_SETFOCUS:
    // The field takes the focus unless a drawn part has it.
    if (m_focus == ITEM_ADDRESS || !isFocusable(m_focus)) {
      m_focus = ITEM_ADDRESS;
      if (m_edit != 0) {
        SetFocus(m_edit);
      }
    }
    invalidate();
    return 0;
  case WM_KILLFOCUS:
    invalidate();
    return 0;
  case WM_KEYDOWN:
    if (onKey(wParam, false)) {
      return 0;
    }
    break;
  case WM_SETCURSOR:
    if (reinterpret_cast<HWND>(wParam) == m_hwnd && LOWORD(lParam) == HTCLIENT) {
      POINT point;
      GetCursorPos(&point);
      ScreenToClient(m_hwnd, &point);
      int item = hitTest(MAKELPARAM(point.x, point.y));
      LPCTSTR cursor = IDC_ARROW;
      if (item == ITEM_ADDRESS) {
        cursor = IDC_IBEAM;
      } else if (item != ITEM_NONE) {
        cursor = IDC_HAND;
      }
      SetCursor(LoadCursor(0, cursor));
      return TRUE;
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
    setHover(ITEM_NONE);
    return 0;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
  {
    int item = hitTest(lParam);
    if (item == ITEM_ADDRESS) {
      // The box around the field.
      if (m_edit != 0) {
        SetFocus(m_edit);
      }
      return 0;
    }
    if (item != ITEM_NONE) {
      m_pressed = item;
      SetCapture(m_hwnd);
      invalidate();
    }
    return 0;
  }
  case WM_LBUTTONUP:
  {
    int pressed = m_pressed;
    if (pressed == ITEM_NONE) {
      return 0;
    }
    m_pressed = ITEM_NONE;
    ReleaseCapture();
    invalidate();
    if (hitTest(lParam) == pressed) {
      if (pressed >= ITEM_FIRST_HOST) {
        m_hostFocus = pressed - ITEM_FIRST_HOST;
      }
      activate(pressed);
    }
    return 0;
  }
  case WM_CAPTURECHANGED:
    if (m_pressed != ITEM_NONE) {
      m_pressed = ITEM_NONE;
      invalidate();
    }
    return 0;
  case WM_MOUSEWHEEL:
  {
    float notches = static_cast<short>(HIWORD(wParam)) / static_cast<float>(WHEEL_DELTA);
    scrollTo(m_scroll - notches * kWheelStep);
    return 0;
  }
  case WM_CONTEXTMENU:
  {
    if (reinterpret_cast<HWND>(wParam) != m_hwnd) {
      break;
    }
    if (lParam == -1) {
      // Menu key or Shift+F10.
      if (m_focus >= ITEM_FIRST_HOST) {
        showHostMenu(m_focus - ITEM_FIRST_HOST, 0);
      }
      return 0;
    }
    POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
    POINT client = point;
    ScreenToClient(m_hwnd, &client);
    int item = hitTest(MAKELPARAM(client.x, client.y));
    if (item >= ITEM_FIRST_HOST) {
      m_hostFocus = item - ITEM_FIRST_HOST;
      showHostMenu(item - ITEM_FIRST_HOST, &point);
    }
    return 0;
  }
  }
  return DefWindowProc(m_hwnd, message, wParam, lParam);
}

bool HomePage::onKey(WPARAM key, bool fromEdit)
{
  bool shift = GetKeyState(VK_SHIFT) < 0;
  bool control = GetKeyState(VK_CONTROL) < 0;
  bool alt = GetKeyState(VK_MENU) < 0;
  bool onHost = !fromEdit && m_focus >= ITEM_FIRST_HOST;
  switch (key) {
  case VK_TAB:
    if (control || alt) {
      return false;
    }
    moveFocus(!shift);
    return true;
  case VK_RETURN:
    activate(fromEdit ? static_cast<int>(ITEM_ADDRESS) : m_focus);
    return true;
  case VK_SPACE:
    if (fromEdit) {
      return false;
    }
    activate(m_focus);
    return true;
  case VK_DOWN:
    if (fromEdit) {
      // From the field into the host list.
      if (m_hosts.empty()) {
        return false;
      }
      setFocusItem(ITEM_FIRST_HOST + static_cast<int>(m_hostFocus));
      return true;
    }
    moveInHosts(1);
    return true;
  case VK_UP:
    if (fromEdit) {
      return false;
    }
    if (onHost && m_focus == ITEM_FIRST_HOST) {
      setFocusItem(ITEM_ADDRESS);
    } else {
      moveInHosts(-1);
    }
    return true;
  case VK_HOME:
  case VK_END:
    if (!onHost) {
      return false;
    }
    setFocusItem(key == VK_HOME ? static_cast<int>(ITEM_FIRST_HOST)
                                : ITEM_FIRST_HOST + static_cast<int>(m_hosts.size()) - 1);
    return true;
  case 'N':
    if (control && !shift && !alt) {
      m_actions->openHome();
      return true;
    }
    return false;
  }
  return false;
}

void HomePage::loadHosts()
{
  ConnectionHistory *history = ViewerConfig::getInstance()->getConnectionHistory();
  history->load();
  std::vector<StringStorage> recents;
  for (size_t i = 0; i < history->getHostCount(); i++) {
    recents.push_back(StringStorage(history->getHost(i)));
  }
  m_favorites.load();
  // Favorites first, then the recents (most recent first, no duplicates).
  std::vector<StringStorage> hosts;
  m_favoriteCount = m_favorites.buildHostList(recents, history->getLimit(), &hosts);

  m_hosts.clear();
  for (size_t i = 0; i < hosts.size(); i++) {
    HostRow row;
    row.host = toWide(hosts[i].getString());
    row.favorite = i < m_favoriteCount;
    row.inHistory = false;
    for (size_t j = 0; j < history->getHostCount() && !row.inHistory; j++) {
      row.inHistory = ConnectionFavorites::isSameHost(history->getHost(j), hosts[i].getString());
    }
    ConnectionData data;
    data.setHost(&hosts[i]);
    StringStorage reduced;
    data.getReducedHost(&reduced);
    row.name = toWide(reduced.getString());
    if (row.name.empty()) {
      row.name = row.host;
    }
    row.address = row.name + L"::" + std::to_wstring(data.getPort());
    row.rect = D2D1::RectF();
    m_hosts.push_back(row);
  }
  if (m_hostFocus >= m_hosts.size()) {
    m_hostFocus = m_hosts.empty() ? 0 : m_hosts.size() - 1;
  }
  if (m_focus >= ITEM_FIRST_HOST) {
    if (m_hosts.empty()) {
      m_focus = ITEM_ADDRESS;
    } else {
      m_focus = ITEM_FIRST_HOST + static_cast<int>(m_hostFocus);
    }
  }
  m_hover = ITEM_NONE;
  m_pressed = ITEM_NONE;
}

void HomePage::refresh()
{
  if (m_hwnd == 0) {
    return;
  }
  bool focusOnHost = m_focus >= ITEM_FIRST_HOST && GetFocus() == m_hwnd;
  loadHosts();
  layout();
  // The focused host may be gone: the field takes the focus back.
  if (focusOnHost && m_focus == ITEM_ADDRESS && m_edit != 0) {
    SetFocus(m_edit);
  }
}

void HomePage::setDpi(UINT dpi)
{
  if (dpi == 0 || dpi == m_chrome.getDpi()) {
    return;
  }
  m_chrome.setDpi(dpi);
  updateEditFont();
  layout();
}

void HomePage::layout()
{
  if (m_hwnd == 0) {
    return;
  }
  const ThemeTokens &theme = kLightTheme;
  RECT client;
  GetClientRect(m_hwnd, &client);
  float width = m_chrome.pixelsToDips(client.right - client.left);

  float column = (std::min)(kColumnWidth, width - 2.0f * kSideMargin);
  if (column < 240.0f) {
    column = (std::max)(width - 2.0f * theme.spacing, 120.0f);
  }
  float left = std::floor((width - column) / 2.0f);
  float right = left + column;
  float y = kTopMargin;

  m_heading = D2D1::RectF(left, y, right, y + kHeadingHeight);
  y += kHeadingHeight + theme.spacingSmall;
  m_subtitle = D2D1::RectF(left, y, right, y + kSubtitleHeight);
  y += kSubtitleHeight + 20.0f;

  m_connect = D2D1::RectF(right - kConnectWidth, y, right, y + kFieldHeight);
  m_field = D2D1::RectF(left, y, m_connect.left - theme.spacing, y + kFieldHeight);
  y += kFieldHeight + 10.0f;

  float optionsWidth = m_chrome.measureText(kOptions, theme.fontSizeBody) +
                       kLinkGlyph + theme.spacing + 2.0f * kLinkPadding;
  m_options = D2D1::RectF(left, y, left + optionsWidth, y + kLinkHeight);
  const wchar_t *listenLabel = m_actions->isListening() ? kStopListening : kListen;
  float listenWidth = m_chrome.measureText(listenLabel, theme.fontSizeBody) +
                      kLinkGlyph + theme.spacing + 2.0f * kLinkPadding;
  m_listen = D2D1::RectF(m_options.right + theme.spacingSmall, y,
                         m_options.right + theme.spacingSmall + listenWidth, y + kLinkHeight);
  m_listenStatus = D2D1::RectF(m_listen.right + theme.spacing, y, right, y + kLinkHeight);
  y += kLinkHeight + kSectionGap;

  m_favoritesHeader = m_recentsHeader = m_empty = D2D1::RectF();
  if (m_hosts.empty()) {
    m_empty = D2D1::RectF(left, y, right, y + kEmptyHeight);
    y += kEmptyHeight;
  }
  for (size_t i = 0; i < m_hosts.size(); i++) {
    if (i == 0 && m_favoriteCount > 0) {
      m_favoritesHeader = D2D1::RectF(left, y, right, y + kHeaderHeight);
      y += kHeaderHeight;
    }
    if (i == m_favoriteCount) {
      if (i > 0) {
        y += kSectionGap - kRowGap;
      }
      m_recentsHeader = D2D1::RectF(left, y, right, y + kHeaderHeight);
      y += kHeaderHeight;
    }
    m_hosts[i].rect = D2D1::RectF(left, y, right, y + kRowHeight);
    y += kRowHeight + kRowGap;
  }
  m_contentHeight = y + kBottomMargin;

  // Keeps the scroll offset inside the new content.
  float offset = m_scroll;
  m_scroll = -1.0f;
  scrollTo(offset);
}

void HomePage::placeEdit()
{
  if (m_edit == 0) {
    return;
  }
  // One line of text, vertically centered in the drawn box.
  int lineHeight = m_chrome.dipsToPixels(kFieldFontSize);
  HDC dc = GetDC(m_edit);
  if (dc != 0) {
    HGDIOBJ old = SelectObject(dc, m_editFont);
    TEXTMETRIC metrics;
    if (GetTextMetrics(dc, &metrics)) {
      lineHeight = metrics.tmHeight;
    }
    SelectObject(dc, old);
    ReleaseDC(m_edit, dc);
  }
  int left = m_chrome.dipsToPixels(m_field.left + kFieldPadding);
  int right = m_chrome.dipsToPixels(m_field.right - kFieldPadding);
  int top = m_chrome.dipsToPixels(m_field.top - m_scroll);
  int bottom = m_chrome.dipsToPixels(m_field.bottom - m_scroll);
  int editHeight = lineHeight + 2;
  int editTop = top + (bottom - top - editHeight) / 2;
  SetWindowPos(m_edit, 0, left, editTop, (std::max)(right - left, 0), editHeight,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void HomePage::scrollTo(float offset)
{
  RECT client;
  GetClientRect(m_hwnd, &client);
  float view = m_chrome.pixelsToDips(client.bottom - client.top);
  float maximum = (std::max)(0.0f, m_contentHeight - view);
  offset = (std::max)(0.0f, (std::min)(offset, maximum));
  if (offset == m_scroll) {
    return;
  }
  m_scroll = offset;
  placeEdit();
  invalidate();
}

D2D1_RECT_F HomePage::itemRect(int item) const
{
  switch (item) {
  case ITEM_ADDRESS:
    return m_field;
  case ITEM_CONNECT:
    return m_connect;
  case ITEM_OPTIONS:
    return m_options;
  case ITEM_LISTEN:
    return m_listen;
  default:
    if (item >= ITEM_FIRST_HOST && static_cast<size_t>(item - ITEM_FIRST_HOST) < m_hosts.size()) {
      return m_hosts[item - ITEM_FIRST_HOST].rect;
    }
    return D2D1::RectF();
  }
}

bool HomePage::isFocusable(int item) const
{
  switch (item) {
  case ITEM_ADDRESS:
  case ITEM_OPTIONS:
  case ITEM_LISTEN:
    return true;
  case ITEM_CONNECT:
    return !fieldText().empty();
  default:
    return item >= ITEM_FIRST_HOST && static_cast<size_t>(item - ITEM_FIRST_HOST) < m_hosts.size();
  }
}

int HomePage::hitTest(LPARAM lParam) const
{
  float x = m_chrome.pixelsToDips(static_cast<short>(LOWORD(lParam)));
  float y = m_chrome.pixelsToDips(static_cast<short>(HIWORD(lParam))) + m_scroll;
  if (contains(m_field, x, y)) {
    return ITEM_ADDRESS;
  }
  if (contains(m_connect, x, y)) {
    return fieldText().empty() ? static_cast<int>(ITEM_NONE) : static_cast<int>(ITEM_CONNECT);
  }
  if (contains(m_options, x, y)) {
    return ITEM_OPTIONS;
  }
  if (contains(m_listen, x, y)) {
    return ITEM_LISTEN;
  }
  for (size_t i = 0; i < m_hosts.size(); i++) {
    if (contains(m_hosts[i].rect, x, y)) {
      return ITEM_FIRST_HOST + static_cast<int>(i);
    }
  }
  return ITEM_NONE;
}

void HomePage::setHover(int item)
{
  if (item != m_hover) {
    m_hover = item;
    invalidate();
  }
}

void HomePage::setFocusItem(int item)
{
  if (!isFocusable(item)) {
    return;
  }
  m_focus = item;
  if (item >= ITEM_FIRST_HOST) {
    m_hostFocus = item - ITEM_FIRST_HOST;
  }
  ensureVisible(item);
  if (item == ITEM_ADDRESS) {
    if (m_edit != 0) {
      SetFocus(m_edit);
    }
  } else if (GetFocus() != m_hwnd) {
    SetFocus(m_hwnd);
  }
  invalidate();
}

void HomePage::moveFocus(bool forward)
{
  // Tab stops: the field, the buttons and the host list (one stop).
  std::vector<int> stops;
  stops.push_back(ITEM_ADDRESS);
  if (isFocusable(ITEM_CONNECT)) {
    stops.push_back(ITEM_CONNECT);
  }
  stops.push_back(ITEM_OPTIONS);
  stops.push_back(ITEM_LISTEN);
  if (!m_hosts.empty()) {
    stops.push_back(ITEM_FIRST_HOST + static_cast<int>(m_hostFocus));
  }
  bool onEdit = GetFocus() == m_edit;
  int current = onEdit ? static_cast<int>(ITEM_ADDRESS) : m_focus;
  if (current >= ITEM_FIRST_HOST) {
    current = ITEM_FIRST_HOST + static_cast<int>(m_hostFocus);
  }
  std::vector<int>::iterator found = std::find(stops.begin(), stops.end(), current);
  size_t index = found == stops.end() ? 0 : found - stops.begin();
  size_t count = stops.size();
  index = forward ? (index + 1) % count : (index + count - 1) % count;
  setFocusItem(stops[index]);
}

void HomePage::moveInHosts(int delta)
{
  if (m_hosts.empty()) {
    return;
  }
  int index = m_focus >= ITEM_FIRST_HOST ? m_focus - ITEM_FIRST_HOST : -1;
  index += delta;
  index = (std::max)(0, (std::min)(index, static_cast<int>(m_hosts.size()) - 1));
  setFocusItem(ITEM_FIRST_HOST + index);
}

void HomePage::ensureVisible(int item)
{
  D2D1_RECT_F rect = itemRect(item);
  RECT client;
  GetClientRect(m_hwnd, &client);
  float view = m_chrome.pixelsToDips(client.bottom - client.top);
  float margin = kLightTheme.spacingLarge;
  if (rect.top - margin < m_scroll) {
    scrollTo(rect.top - margin);
  } else if (rect.bottom + margin > m_scroll + view) {
    scrollTo(rect.bottom + margin - view);
  }
}

void HomePage::activate(int item)
{
  switch (item) {
  case ITEM_ADDRESS:
  case ITEM_CONNECT:
    connectFromField();
    break;
  case ITEM_OPTIONS:
    showOptions();
    break;
  case ITEM_LISTEN:
    // The application refreshes every Home page (refresh()).
    m_actions->toggleListening();
    break;
  default:
    if (item >= ITEM_FIRST_HOST && static_cast<size_t>(item - ITEM_FIRST_HOST) < m_hosts.size()) {
      std::wstring host = m_hosts[item - ITEM_FIRST_HOST].host;
      connect(host, savedConfig(host));
    }
    break;
  }
}

std::wstring HomePage::fieldText() const
{
  if (m_edit == 0) {
    return std::wstring();
  }
  int length = GetWindowTextLengthW(m_edit);
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring text(length + 1, L'\0');
  length = GetWindowTextW(m_edit, &text[0], length + 1);
  text.resize(length > 0 ? length : 0);
  return text;
}

void HomePage::connectFromField()
{
  std::wstring host = fieldText();
  if (host.empty()) {
    if (m_edit != 0) {
      SetFocus(m_edit);
    }
    return;
  }
  // The options chosen for this connection, or the ones saved for the host.
  connect(host, m_optionsEdited ? m_connectionConfig : savedConfig(host));
}

void HomePage::connect(const std::wstring &host, const ConnectionConfig &config)
{
  // A double click or a repeated Enter must not start a second session in
  // the shell: the session replaces this page a moment later.
  if (m_connecting) {
    return;
  }
  m_connecting = true;
  StringStorage hostName = toStorage(host);
  m_actions->connectFromHome(m_shell, &hostName, &config);
}

void HomePage::showOptions()
{
  // Like the Options button of the former login dialog: the options of the
  // next connection, starting from the ones saved for the host in the field.
  if (!m_optionsEdited) {
    m_connectionConfig = savedConfig(fieldText());
  }
  std::shared_ptr<bool> alive = m_alive;
  ConnectionConfig config(m_connectionConfig);
  OptionsDialog dialog;
  dialog.setConnectionConfig(&config);
  Control owner(m_shell);
  dialog.setParent(&owner);
  if (dialog.showModal() == 1 && *alive) {
    m_connectionConfig = config;
    m_optionsEdited = true;
  }
}

void HomePage::showHostMenu(size_t index, const POINT *screenPoint)
{
  if (index >= m_hosts.size()) {
    return;
  }
  const HostRow &row = m_hosts[index];
  std::wstring host = row.host;
  bool favorite = row.favorite;
  POINT point;
  if (screenPoint != 0) {
    point = *screenPoint;
  } else {
    // Keyboard: below the start of the row.
    point.x = m_chrome.dipsToPixels(row.rect.left + kHostIcon);
    point.y = m_chrome.dipsToPixels(row.rect.bottom - m_scroll);
    ClientToScreen(m_hwnd, &point);
  }

  HMENU menu = CreatePopupMenu();
  if (menu == 0) {
    return;
  }
  AppendMenuW(menu, MF_STRING, MENU_CONNECT, kMenuConnect);
  SetMenuDefaultItem(menu, MENU_CONNECT, FALSE);
  AppendMenuW(menu, MF_SEPARATOR, 0, 0);
  AppendMenuW(menu, MF_STRING, MENU_FAVORITE, favorite ? kMenuRemoveFavorite : kMenuAddFavorite);
  if (row.inHistory) {
    AppendMenuW(menu, MF_STRING, MENU_REMOVE_RECENT, kMenuRemoveRecent);
  }
  setFocusItem(ITEM_FIRST_HOST + static_cast<int>(index));

  std::shared_ptr<bool> alive = m_alive;
  int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                               point.x, point.y, 0, m_hwnd, 0);
  DestroyMenu(menu);
  if (!*alive) {
    return;
  }

  StringStorage hostName = toStorage(host);
  switch (command) {
  case MENU_CONNECT:
    connect(host, savedConfig(host));
    return;
  case MENU_FAVORITE:
    m_favorites.load();
    if (favorite ? m_favorites.remove(hostName.getString()) : m_favorites.add(hostName.getString())) {
      m_favorites.save();
    } else if (!favorite) {
      // The favorites are full.
      MessageBeep(MB_ICONWARNING);
    }
    refresh();
    return;
  case MENU_REMOVE_RECENT:
  {
    ConnectionHistory *history = ViewerConfig::getInstance()->getConnectionHistory();
    m_favorites.load();
    // A favorite keeps its saved options; other hosts lose them with the entry.
    history->removeEntry(hostName.getString(), !m_favorites.contains(hostName.getString()));
    refresh();
    return;
  }
  }
}

void HomePage::invalidate()
{
  if (m_hwnd != 0) {
    InvalidateRect(m_hwnd, 0, FALSE);
  }
}

void HomePage::paint()
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(m_hwnd, &ps);
  if (dc == 0) {
    return;
  }
  const ThemeTokens &theme = kLightTheme;
  if (!m_chrome.beginDraw()) {
    // No Direct2D: the background only; the field still works.
    HBRUSH brush = CreateSolidBrush(toColorRef(theme.background));
    FillRect(dc, &ps.rcPaint, brush);
    DeleteObject(brush);
    EndPaint(m_hwnd, &ps);
    return;
  }
  m_chrome.clear(theme.background);
  float scroll = m_scroll;
  auto shifted = [scroll](const D2D1_RECT_F &rect) {
    return D2D1::RectF(rect.left, rect.top - scroll, rect.right, rect.bottom - scroll);
  };

  m_chrome.drawText(kHeading, shifted(m_heading), theme.text, kHeadingSize,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD);
  m_chrome.drawText(kSubtitle, shifted(m_subtitle), theme.mutedText, theme.fontSizeBody);

  // Address field: a rounded box around the borderless EDIT.
  bool fieldFocused = m_edit != 0 && GetFocus() == m_edit;
  D2D1_RECT_F field = shifted(m_field);
  m_chrome.fillRoundRect(field, kFieldRadius, theme.background);
  if (fieldFocused) {
    m_chrome.drawRoundRect(inflate(field, -1.0f), kFieldRadius - 1.0f, theme.accent, 2.0f);
  } else {
    ThemeColor border = m_hover == ITEM_ADDRESS ? theme.disabledText : theme.border;
    m_chrome.drawRoundRect(inflate(field, -0.5f), kFieldRadius, border, 1.0f);
  }

  // Connect: the primary button, disabled while the field is empty.
  bool canConnect = !fieldText().empty();
  D2D1_RECT_F connectRect = shifted(m_connect);
  ThemeColor connectFill = theme.border;
  if (canConnect) {
    connectFill = theme.accent;
    if (m_pressed == ITEM_CONNECT) {
      connectFill = theme.accentPressed;
    } else if (m_hover == ITEM_CONNECT) {
      connectFill = theme.accentHover;
    }
  }
  m_chrome.fillRoundRect(connectRect, kFieldRadius, connectFill);
  m_chrome.drawText(kConnect, connectRect, canConnect ? theme.accentText : theme.disabledText,
                    theme.fontSizeBody, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
  if (m_focus == ITEM_CONNECT && GetFocus() == m_hwnd && canConnect) {
    m_chrome.drawRoundRect(inflate(connectRect, 2.5f), kFieldRadius + 2.5f, theme.accent, 2.0f);
  }

  bool listening = m_actions->isListening();
  paintButton(ITEM_OPTIONS, kOptions, kGlyphOptions);
  paintButton(ITEM_LISTEN, listening ? kStopListening : kListen, kGlyphListen);
  if (listening) {
    D2D1_RECT_F status = shifted(m_listenStatus);
    float dot = 8.0f;
    float centerY = (status.top + status.bottom) / 2.0f;
    m_chrome.fillRoundRect(D2D1::RectF(status.left, centerY - dot / 2.0f,
                                       status.left + dot, centerY + dot / 2.0f),
                           dot / 2.0f, theme.accent);
    wchar_t text[96];
    swprintf_s(text, kListeningFormat, ViewerConfig::getInstance()->getListenPort());
    m_chrome.drawText(text, D2D1::RectF(status.left + dot + theme.spacing, status.top,
                                        status.right, status.bottom),
                      theme.mutedText, theme.fontSizeSmall);
  }

  if (m_hosts.empty()) {
    paintEmptyState();
  }
  if (m_favoriteCount > 0) {
    m_chrome.drawText(kFavorites, shifted(m_favoritesHeader), theme.mutedText,
                      theme.fontSizeSmall, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  }
  if (m_hosts.size() > m_favoriteCount) {
    m_chrome.drawText(kRecents, shifted(m_recentsHeader), theme.mutedText,
                      theme.fontSizeSmall, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  }
  for (size_t i = 0; i < m_hosts.size(); i++) {
    paintHost(i);
  }
  m_chrome.endDraw();
  EndPaint(m_hwnd, &ps);
}

void HomePage::paintButton(int item, const wchar_t *label, wchar_t glyph)
{
  // Secondary action: glyph and label, a background only on hover/press.
  const ThemeTokens &theme = kLightTheme;
  D2D1_RECT_F rect = itemRect(item);
  rect.top -= m_scroll;
  rect.bottom -= m_scroll;
  if (m_pressed == item) {
    m_chrome.fillRoundRect(rect, theme.cornerRadius, theme.hover);
  } else if (m_hover == item) {
    m_chrome.fillRoundRect(rect, theme.cornerRadius, theme.surface);
  }
  D2D1_RECT_F icon = D2D1::RectF(rect.left + kLinkPadding, rect.top,
                                 rect.left + kLinkPadding + kLinkGlyph, rect.bottom);
  m_chrome.drawIconGlyph(glyph, icon, theme.text, kLinkGlyph - 2.0f);
  m_chrome.drawText(label, D2D1::RectF(icon.right + theme.spacing, rect.top,
                                       rect.right, rect.bottom),
                    theme.text, theme.fontSizeBody);
  if (m_focus == item && GetFocus() == m_hwnd) {
    m_chrome.drawRoundRect(inflate(rect, -1.0f), theme.cornerRadius - 1.0f, theme.accent, 2.0f);
  }
}

void HomePage::paintHost(size_t index)
{
  const ThemeTokens &theme = kLightTheme;
  const HostRow &row = m_hosts[index];
  int item = ITEM_FIRST_HOST + static_cast<int>(index);
  D2D1_RECT_F rect = D2D1::RectF(row.rect.left, row.rect.top - m_scroll,
                                 row.rect.right, row.rect.bottom - m_scroll);
  bool highlighted = m_hover == item || m_pressed == item;
  if (m_pressed == item) {
    m_chrome.fillRoundRect(rect, kFieldRadius, theme.hover);
  } else if (m_hover == item) {
    m_chrome.fillRoundRect(rect, kFieldRadius, theme.surface);
  }
  if (m_focus == item && GetFocus() == m_hwnd) {
    m_chrome.drawRoundRect(inflate(rect, -1.0f), kFieldRadius - 1.0f, theme.accent, 2.0f);
  }

  float centerY = (rect.top + rect.bottom) / 2.0f;
  float iconLeft = rect.left + 10.0f;
  D2D1_RECT_F icon = D2D1::RectF(iconLeft, centerY - kHostIcon / 2.0f,
                                 iconLeft + kHostIcon, centerY + kHostIcon / 2.0f);
  m_chrome.fillRoundRect(icon, kFieldRadius, highlighted ? theme.background : theme.surface);
  m_chrome.drawIconGlyph(kGlyphHost, icon, theme.text, 18.0f);

  float textLeft = icon.right + 12.0f;
  float textRight = rect.right - 44.0f;
  m_chrome.drawText(row.name.c_str(), D2D1::RectF(textLeft, rect.top + 8.0f, textRight, centerY + 1.0f),
                    theme.text, theme.fontSizeBody, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  m_chrome.drawText(row.address.c_str(), D2D1::RectF(textLeft, centerY + 1.0f, textRight, rect.bottom - 8.0f),
                    theme.mutedText, theme.fontSizeSmall);
  if (row.favorite) {
    D2D1_RECT_F star = D2D1::RectF(rect.right - 36.0f, centerY - 10.0f, rect.right - 16.0f, centerY + 10.0f);
    m_chrome.drawIconGlyph(kGlyphFavorite, star, theme.accent, 14.0f);
  }
}

void HomePage::paintEmptyState()
{
  const ThemeTokens &theme = kLightTheme;
  D2D1_RECT_F area = D2D1::RectF(m_empty.left, m_empty.top - m_scroll,
                                 m_empty.right, m_empty.bottom - m_scroll);
  float centerX = (area.left + area.right) / 2.0f;
  float circle = 56.0f;
  D2D1_RECT_F icon = D2D1::RectF(centerX - circle / 2.0f, area.top + 8.0f,
                                 centerX + circle / 2.0f, area.top + 8.0f + circle);
  m_chrome.fillRoundRect(icon, circle / 2.0f, theme.surface);
  m_chrome.drawIconGlyph(kGlyphHost, icon, theme.mutedText, 24.0f);
  float y = icon.bottom + 14.0f;
  m_chrome.drawText(kEmptyTitle, D2D1::RectF(area.left, y, area.right, y + 22.0f), theme.text,
                    theme.fontSizeBody + 1.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_TEXT_ALIGNMENT_CENTER);
  y += 24.0f;
  m_chrome.drawText(kEmptyLine1, D2D1::RectF(area.left, y, area.right, y + 18.0f), theme.mutedText,
                    theme.fontSizeSmall + 1.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
  y += 18.0f;
  m_chrome.drawText(kEmptyLine2, D2D1::RectF(area.left, y, area.right, y + 18.0f), theme.mutedText,
                    theme.fontSizeSmall + 1.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
}
