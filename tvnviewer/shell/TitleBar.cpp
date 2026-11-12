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
// Modified for FarSightVNC on 2026-11-12: "+" button (new connection, Lucide icon) right after the tab.
//

#include "TitleBar.h"

#include "../resource.h"

namespace {

// Sizes in DIPs. The window buttons follow the Windows 11 caption buttons.
const float kCaptionButtonWidth = 46.0f;
const float kSettingsWidth = 40.0f;
const float kLogoSize = 20.0f;
const float kTabMaxWidth = 240.0f;
const float kTabMinWidth = 72.0f;
const float kTabGap = 4.0f;
const float kNewTabWidth = 28.0f;
const float kTabVerticalMargin = 6.0f;
const float kCaptionGlyphSize = 10.0f;
const float kSettingsGlyphSize = 14.0f;
const float kNewTabIconSize = 16.0f;

// Codepoints of Segoe Fluent Icons / Segoe MDL2 Assets. The icon phase
// replaces them with Lucide icons.
const wchar_t kGlyphMinimize = 0xE921;
const wchar_t kGlyphMaximize = 0xE922;
const wchar_t kGlyphRestore = 0xE923;
const wchar_t kGlyphClose = 0xE8BB;
const wchar_t kGlyphSettings = 0xE713;
// Fallback of the Lucide "plus" icon (Windows 10 before 1703).
const wchar_t kGlyphAdd = 0xE710;

bool contains(const D2D1_RECT_F &rect, float x, float y)
{
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

D2D1_RECT_F inset(const D2D1_RECT_F &rect, float dx, float dy)
{
  return D2D1::RectF(rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy);
}

}

TabStripLayout::TabStripLayout()
: m_newTab(D2D1::RectF())
{
}

void TabStripLayout::layout(float left, float right, float top, float bottom, size_t count)
{
  m_tabs.clear();
  float available = right - left - kNewTabWidth - kTabGap * count;
  float width = 0.0f;
  if (count > 0 && available > 0.0f) {
    width = available / count;
    if (width > kTabMaxWidth) {
      width = kTabMaxWidth;
    }
  }
  // Below the minimum width a tab cannot show its title: the strip keeps
  // the space of the tabs but draws none.
  float x = left;
  for (size_t i = 0; i < count; i++) {
    float w = width >= kTabMinWidth ? width : 0.0f;
    m_tabs.push_back(D2D1::RectF(x, top, x + w, bottom));
    x += w + kTabGap;
  }
  m_newTab = D2D1::RectF(x, top, x + kNewTabWidth, bottom);
  if (m_newTab.right > right) {
    m_newTab.right = m_newTab.left;
  }
}

TitleBar::TitleBar()
: m_width(0.0f),
  m_logo(D2D1::RectF()),
  m_settings(D2D1::RectF()),
  m_minimize(D2D1::RectF()),
  m_maximize(D2D1::RectF()),
  m_close(D2D1::RectF()),
  m_empty(D2D1::RectF()),
  m_hover(TITLE_BAR_NONE),
  m_pressed(TITLE_BAR_NONE),
  m_settingsActive(false)
{
}

float TitleBar::height() const
{
  return kLightTheme.titleBarHeight;
}

void TitleBar::layout(float width)
{
  const ThemeTokens &theme = kLightTheme;
  float h = height();
  m_width = width;

  m_close = D2D1::RectF(width - kCaptionButtonWidth, 0.0f, width, h);
  m_maximize = D2D1::RectF(m_close.left - kCaptionButtonWidth, 0.0f, m_close.left, h);
  m_minimize = D2D1::RectF(m_maximize.left - kCaptionButtonWidth, 0.0f, m_maximize.left, h);
  m_settings = D2D1::RectF(m_minimize.left - theme.spacingSmall - kSettingsWidth, 0.0f,
                           m_minimize.left - theme.spacingSmall, h);

  float logoTop = (h - kLogoSize) / 2.0f;
  m_logo = D2D1::RectF(theme.spacing + theme.spacingSmall, logoTop,
                       theme.spacing + theme.spacingSmall + kLogoSize, logoTop + kLogoSize);

  // One tab for now: the strip is already laid out for several.
  float stripLeft = m_logo.right + theme.spacing + theme.spacingSmall;
  float stripRight = m_settings.left - theme.spacing;
  if (stripRight < stripLeft) {
    stripRight = stripLeft;
  }
  m_tabs.layout(stripLeft, stripRight, kTabVerticalMargin, h - kTabVerticalMargin, 1);
}

TitleBarPart TitleBar::hitTest(float x, float y) const
{
  if (y < 0.0f || y >= height() || x < 0.0f || x >= m_width) {
    return TITLE_BAR_NONE;
  }
  if (contains(m_close, x, y)) {
    return TITLE_BAR_CLOSE;
  }
  if (contains(m_maximize, x, y)) {
    return TITLE_BAR_MAXIMIZE;
  }
  if (contains(m_minimize, x, y)) {
    return TITLE_BAR_MINIMIZE;
  }
  if (contains(m_settings, x, y)) {
    return TITLE_BAR_SETTINGS;
  }
  for (size_t i = 0; i < m_tabs.count(); i++) {
    if (contains(m_tabs.tab(i), x, y)) {
      return TITLE_BAR_TAB;
    }
  }
  if (contains(m_tabs.newTabSlot(), x, y)) {
    return TITLE_BAR_NEW_TAB;
  }
  return TITLE_BAR_CAPTION;
}

void TitleBar::setTitle(const std::wstring &title)
{
  m_title = title;
}

bool TitleBar::setHover(TitleBarPart part)
{
  if (part == m_hover) {
    return false;
  }
  m_hover = part;
  return true;
}

bool TitleBar::setPressed(TitleBarPart part)
{
  if (part == m_pressed) {
    return false;
  }
  m_pressed = part;
  return true;
}

const D2D1_RECT_F &TitleBar::partRect(TitleBarPart part) const
{
  switch (part) {
  case TITLE_BAR_SETTINGS:
    return m_settings;
  case TITLE_BAR_MINIMIZE:
    return m_minimize;
  case TITLE_BAR_MAXIMIZE:
    return m_maximize;
  case TITLE_BAR_CLOSE:
    return m_close;
  case TITLE_BAR_NEW_TAB:
    return m_tabs.newTabSlot();
  default:
    return m_empty;
  }
}

bool TitleBar::buttonBackground(TitleBarPart part, ThemeColor *color) const
{
  const ThemeTokens &theme = kLightTheme;
  // A pressed button shows its pressed color only while the pointer is
  // still over it, like the native buttons.
  bool pressed = m_pressed == part && m_hover == part;
  bool hover = m_pressed == TITLE_BAR_NONE ? m_hover == part : pressed;
  if (part == TITLE_BAR_CLOSE) {
    if (pressed) {
      *color = theme.closePressed;
      return true;
    }
    if (hover) {
      *color = theme.closeHover;
      return true;
    }
    return false;
  }
  if (pressed) {
    *color = theme.pressed;
    return true;
  }
  if (hover) {
    *color = part == TITLE_BAR_SETTINGS && m_settingsActive ? theme.selectedHover : theme.hover;
    return true;
  }
  if (part == TITLE_BAR_SETTINGS && m_settingsActive) {
    *color = theme.selected;
    return true;
  }
  return false;
}

void TitleBar::paintCaptionButton(ChromeRenderer *renderer, TitleBarPart part, wchar_t icon,
                                  const wchar_t *placeholder, bool active) const
{
  const ThemeTokens &theme = kLightTheme;
  const D2D1_RECT_F &rect = partRect(part);
  ThemeColor background;
  ThemeColor glyph = active ? theme.text : theme.mutedText;
  bool highlighted = buttonBackground(part, &background);
  if (highlighted) {
    if (part == TITLE_BAR_SETTINGS) {
      renderer->fillRoundRect(inset(rect, theme.spacingSmall, theme.spacingSmall * 1.5f),
                              theme.cornerRadius, background);
    } else {
      renderer->fillRect(rect, background);
    }
    if (part == TITLE_BAR_CLOSE) {
      glyph = theme.closeHoverText;
    } else if (part == TITLE_BAR_SETTINGS && m_settingsActive) {
      glyph = theme.selectedText;
    }
  }
  float size = part == TITLE_BAR_SETTINGS ? kSettingsGlyphSize : kCaptionGlyphSize;
  if (!renderer->drawIconGlyph(icon, rect, glyph, size)) {
    renderer->drawText(placeholder, rect, glyph, theme.fontSizeSmall,
                       DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
  }
}

void TitleBar::paintNewTabButton(ChromeRenderer *renderer, bool active) const
{
  const ThemeTokens &theme = kLightTheme;
  const D2D1_RECT_F &rect = m_tabs.newTabSlot();
  if (rect.right - rect.left <= 0.0f) {
    return;
  }
  ThemeColor background;
  if (buttonBackground(TITLE_BAR_NEW_TAB, &background)) {
    renderer->fillRoundRect(rect, theme.cornerRadius, background);
  }
  ThemeColor glyph = active ? theme.text : theme.mutedText;
  float cx = (rect.left + rect.right) / 2.0f;
  float cy = (rect.top + rect.bottom) / 2.0f;
  float half = kNewTabIconSize / 2.0f;
  D2D1_RECT_F icon = D2D1::RectF(cx - half, cy - half, cx + half, cy + half);
  // The Lucide icon, else the system icon font, else a text "+".
  if (!renderer->drawSvgIcon(IDR_LUCIDE_PLUS, icon, glyph) &&
      !renderer->drawIconGlyph(kGlyphAdd, rect, glyph, kNewTabIconSize - 2.0f)) {
    renderer->drawGlyph(L"+", rect, glyph);
  }
}

void TitleBar::paint(ChromeRenderer *renderer, bool active, bool maximized) const
{
  const ThemeTokens &theme = kLightTheme;
  float h = height();
  ThemeColor text = active ? theme.text : theme.mutedText;

  renderer->fillRect(D2D1::RectF(0.0f, 0.0f, m_width, h), theme.surface);
  // Separator between the bar and the content, one pixel thick.
  float line = renderer->pixelsToDips(1);
  renderer->fillRect(D2D1::RectF(0.0f, h - line, m_width, h), theme.border);

  // Placeholder logo until the icon phase.
  renderer->fillRoundRect(m_logo, theme.cornerRadius - 1.0f,
                          active ? theme.accent : theme.mutedText);
  renderer->drawText(L"F", m_logo, theme.closeHoverText, theme.fontSizeSmall + 1.0f,
                     DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);

  for (size_t i = 0; i < m_tabs.count(); i++) {
    const D2D1_RECT_F &tab = m_tabs.tab(i);
    if (tab.right - tab.left <= 0.0f) {
      continue;
    }
    renderer->fillRoundRect(tab, theme.cornerRadius, theme.background);
    renderer->drawRoundRect(tab, theme.cornerRadius, theme.border, line);
    D2D1_RECT_F label = D2D1::RectF(tab.left + theme.spacing + theme.spacingSmall, tab.top,
                                    tab.right - theme.spacing - theme.spacingSmall, tab.bottom);
    if (label.right > label.left) {
      renderer->drawText(m_title.c_str(), label, text, theme.fontSizeSmall);
    }
  }

  paintNewTabButton(renderer, active);
  paintCaptionButton(renderer, TITLE_BAR_SETTINGS, kGlyphSettings, L"\x2699", active);
  paintCaptionButton(renderer, TITLE_BAR_MINIMIZE, kGlyphMinimize, L"\x2014", active);
  paintCaptionButton(renderer, TITLE_BAR_MAXIMIZE, maximized ? kGlyphRestore : kGlyphMaximize,
                     maximized ? L"\x2750" : L"\x25A1", active);
  paintCaptionButton(renderer, TITLE_BAR_CLOSE, kGlyphClose, L"\x2715", active);
}
