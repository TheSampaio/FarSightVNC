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

#include "SettingsPage.h"

#include "SettingsSections.h"
#include "gui/DpiUtils.h"
#include "../DefaultConnectionConfig.h"

#include <algorithm>
#include <cmath>

namespace {

const wchar_t kWindowClassName[] = L"FsvSettingsPage";
const wchar_t kTitle[] = L"Settings";
const wchar_t kBack[] = L"Back";
const wchar_t kGlyphBack = 0xE72B;   // Back (Lucide: arrow-left)

// Child control IDs.
const UINT ID_BACK = 0x5E01;
const UINT ID_NAV = 0x5E02;

// Layout in DIPs, on an 8 DIP grid.
const float kColumnWidth = 232.0f;
const float kColumnPadding = 16.0f;
const float kBackTop = 12.0f;
const float kTitleTop = 60.0f;
const float kTitleHeight = 32.0f;
const float kTitleSize = 20.0f;
const float kNavTop = 104.0f;

}

SettingsPage::SettingsPage(HWND shell, SettingsActions *actions, SettingsSectionId section)
: m_hwnd(0),
  m_dialogHwnd(0),
  m_shell(shell),
  m_actions(actions),
  m_sectionId(section)
{
  DefaultConnectionConfig::load(&m_defaultConfig);
  if (!registerClass()) {
    return;
  }
  // A control parent: Tab reaches the controls of the form inside it.
  CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClassName, kTitle,
                  WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                  0, 0, 0, 0, shell, 0, GetModuleHandle(0), this);
  if (m_hwnd == 0) {
    return;
  }
  m_chrome.attach(m_hwnd, DpiUtils::getDpiForWindow(m_hwnd));

  // Creation order is the tab order: Back, the navigation, the form.
  std::vector<SettingsNav::Item> back(1);
  back[0].glyph = kGlyphBack;
  back[0].label = kBack;
  m_back.reset(new SettingsNav(m_hwnd, ID_BACK, SettingsNav::MODE_BUTTON, back));
  std::vector<SettingsNav::Item> sections(SETTINGS_SECTION_COUNT);
  for (int i = 0; i < SETTINGS_SECTION_COUNT; i++) {
    sections[i].glyph = SettingsSections::glyph(static_cast<SettingsSectionId>(i));
    sections[i].label = SettingsSections::title(static_cast<SettingsSectionId>(i));
  }
  m_nav.reset(new SettingsNav(m_hwnd, ID_NAV, SettingsNav::MODE_LIST, sections));
  m_form.reset(new SettingsForm(m_hwnd, this));

  m_dialogHwnd = m_hwnd;
  m_actions->addDialogWindow(m_dialogHwnd);
  showSection(section);
}

SettingsPage::~SettingsPage()
{
  if (m_dialogHwnd != 0) {
    m_actions->removeDialogWindow(m_dialogHwnd);
  }
  if (m_hwnd != 0) {
    commit();
  }
  // The section goes before its controls.
  m_section.reset();
  m_form.reset();
  m_nav.reset();
  m_back.reset();
  if (m_hwnd != 0) {
    DestroyWindow(m_hwnd);
  }
}

UINT SettingsPage::closeMessage()
{
  static UINT message = RegisterWindowMessageW(L"FarSightVNC.CloseSettings");
  return message;
}

bool SettingsPage::registerClass()
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

HWND SettingsPage::focusHwnd() const
{
  return m_nav ? m_nav->getHWnd() : m_hwnd;
}

void SettingsPage::setDpi(UINT dpi)
{
  if (dpi == 0 || dpi == m_chrome.getDpi()) {
    return;
  }
  m_chrome.setDpi(dpi);
  if (m_back) {
    m_back->setDpi(dpi);
  }
  if (m_nav) {
    m_nav->setDpi(dpi);
  }
  if (m_form) {
    m_form->setDpi(dpi);
  }
  layout();
}

void SettingsPage::commit()
{
  if (m_section) {
    m_section->commit();
  }
}

std::unique_ptr<SettingsSection> SettingsPage::createSection(SettingsSectionId section)
{
  return SettingsSections::create(section, &m_defaultConfig, m_actions);
}

void SettingsPage::showSection(SettingsSectionId section)
{
  if (m_hwnd == 0 || !m_form || section >= SETTINGS_SECTION_COUNT) {
    return;
  }
  commit();
  m_sectionId = section;
  m_nav->select(section);
  m_section.reset();
  m_form->clear();
  m_section = createSection(section);
  m_section->build(m_form.get());
  m_form->layout();
}

LRESULT CALLBACK SettingsPage::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  SettingsPage *page;
  if (message == WM_NCCREATE) {
    CREATESTRUCT *create = reinterpret_cast<CREATESTRUCT *>(lParam);
    page = static_cast<SettingsPage *>(create->lpCreateParams);
    page->m_hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(page));
  } else {
    page = reinterpret_cast<SettingsPage *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  if (page == 0) {
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    page->m_hwnd = 0;
    return DefWindowProc(hwnd, message, wParam, lParam);
  }
  return page->onMessage(message, wParam, lParam);
}

LRESULT SettingsPage::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
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
  case WM_SETFOCUS:
    if (m_nav) {
      SetFocus(m_nav->getHWnd());
    }
    return 0;
  case WM_NEXTDLGCTL:
    // The dialog manager (IsDialogMessage) asks the page to move the focus:
    // DefDlgProc is not there to do it.
    moveFocus(wParam, lParam);
    return 0;
  case WM_COMMAND:
  {
    UINT id = LOWORD(wParam);
    UINT code = HIWORD(wParam);
    HWND control = reinterpret_cast<HWND>(lParam);
    if (id == IDCANCEL && control == 0) {
      // Esc.
      back();
      return 0;
    }
    if (id == ID_BACK && code == SettingsNav::NOTIFY_CLICK) {
      back();
      return 0;
    }
    if (id == ID_NAV && code == SettingsNav::NOTIFY_SELECT) {
      showSection(static_cast<SettingsSectionId>(m_nav->selected()));
      return 0;
    }
    // Enter on a push button of the form is sent here by the dialog manager.
    if (control != 0 && m_form && IsChild(m_form->getHWnd(), control)) {
      onFormCommand(id, code, control);
    }
    return 0;
  }
  }
  return DefWindowProc(m_hwnd, message, wParam, lParam);
}

void SettingsPage::moveFocus(WPARAM wParam, LPARAM lParam)
{
  HWND target;
  if (LOWORD(lParam) != 0) {
    target = reinterpret_cast<HWND>(wParam);
  } else {
    HWND focus = GetFocus();
    if (focus == 0 || (focus != m_hwnd && !IsChild(m_hwnd, focus))) {
      focus = 0;
    }
    target = GetNextDlgTabItem(m_hwnd, focus, wParam != 0);
    // Like the dialog manager: Tab into a group of radio buttons goes to the
    // checked one.
    if (target != 0 &&
        (SendMessage(target, WM_GETDLGCODE, 0, 0) & DLGC_RADIOBUTTON) != 0 &&
        SendMessage(target, BM_GETCHECK, 0, 0) != BST_CHECKED) {
      HWND item = target;
      do {
        item = GetNextDlgGroupItem(m_hwnd, item, FALSE);
        if (item != 0 && SendMessage(item, BM_GETCHECK, 0, 0) == BST_CHECKED) {
          target = item;
          break;
        }
      } while (item != 0 && item != target);
    }
  }
  if (target == 0) {
    return;
  }
  SetFocus(target);
  if ((SendMessage(target, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL) != 0) {
    SendMessage(target, EM_SETSEL, 0, -1);
  }
}

void SettingsPage::onFormCommand(UINT controlId, UINT notification, HWND control)
{
  if (m_section) {
    m_section->onCommand(controlId, notification);
  }
}

void SettingsPage::onFormHScroll(HWND control)
{
  if (m_section) {
    m_section->onHScroll(control);
  }
}

void SettingsPage::back()
{
  commit();
  // The shell destroys this page: not from inside its own messages.
  PostMessage(m_shell, closeMessage(), 0, 0);
}

void SettingsPage::layout()
{
  if (m_hwnd == 0) {
    return;
  }
  RECT client;
  GetClientRect(m_hwnd, &client);
  int column = m_chrome.dipsToPixels(kColumnWidth);
  int padding = m_chrome.dipsToPixels(kColumnPadding);
  if (m_back) {
    SetWindowPos(m_back->getHWnd(), 0, padding, m_chrome.dipsToPixels(kBackTop),
                 column - 2 * padding, m_chrome.dipsToPixels(SettingsNav::itemPitch()),
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (m_nav) {
    SetWindowPos(m_nav->getHWnd(), 0, padding, m_chrome.dipsToPixels(kNavTop),
                 column - 2 * padding, m_chrome.dipsToPixels(m_nav->contentHeight()),
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (m_form) {
    SetWindowPos(m_form->getHWnd(), 0, column, 0, (std::max)(0L, client.right - column),
                 client.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  InvalidateRect(m_hwnd, 0, FALSE);
}

void SettingsPage::paint()
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(m_hwnd, &ps);
  if (dc == 0) {
    return;
  }
  const ThemeTokens &theme = kLightTheme;
  if (!m_chrome.beginDraw()) {
    FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
    EndPaint(m_hwnd, &ps);
    return;
  }
  // The left column; the form covers the rest (WS_CLIPCHILDREN).
  RECT client;
  GetClientRect(m_hwnd, &client);
  float height = m_chrome.pixelsToDips(client.bottom);
  m_chrome.clear(theme.surface);
  float line = m_chrome.pixelsToDips(1);
  m_chrome.fillRect(D2D1::RectF(kColumnWidth - line, 0.0f, kColumnWidth, height), theme.border);
  m_chrome.drawText(kTitle, D2D1::RectF(kColumnPadding + 12.0f, kTitleTop,
                                        kColumnWidth - kColumnPadding, kTitleTop + kTitleHeight),
                    theme.text, kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
  m_chrome.endDraw();
  EndPaint(m_hwnd, &ps);
}
