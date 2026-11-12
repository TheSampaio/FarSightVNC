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

#ifndef _THEME_TOKENS_H_
#define _THEME_TOKENS_H_

// Color as 0xRRGGBB (the layout of D2D1::ColorF(UINT32)), always opaque.
typedef unsigned int ThemeColor;

// Design tokens of the window chrome drawn with Direct2D (title bar, toolbar,
// pages). Sizes are in DIPs (1/96 inch); the renderer scales them to the DPI
// of the window. There is a light and a dark palette; Theme::tokens()
// (gui/d2d/Theme.h) gives the one in use.
struct ThemeTokens
{
  // Colors.
  ThemeColor background;   // Window and page background.
  ThemeColor surface;      // Title bar, toolbar, cards.
  ThemeColor field;        // Background of a native input or a drawn field box.
  ThemeColor border;       // Separators and outlines.
  ThemeColor text;         // Primary text and glyphs.
  ThemeColor mutedText;    // Secondary text, disabled glyphs.
  ThemeColor accent;       // Selection, focus, primary button.
  ThemeColor accentHover;  // Primary button hovered.
  ThemeColor accentPressed; // Primary button pressed.
  ThemeColor accentText;   // Text on the accent color.
  ThemeColor hover;        // Background of a hovered button.
  ThemeColor closeHover;   // Background of the hovered close button.
  ThemeColor closeHoverText;
  ThemeColor pressed;      // Background of a pressed button.
  ThemeColor closePressed; // Background of the pressed close button.
  ThemeColor selected;     // Subtle background of a toggle that is on.
  ThemeColor selectedHover;
  ThemeColor selectedPressed;
  ThemeColor selectedText; // Glyph of a toggle that is on.
  ThemeColor disabledText; // Glyph of a disabled button.

  // Shapes and sizes, in DIPs.
  float cornerRadius;
  float titleBarHeight;
  float toolbarHeight;
  float buttonSize;        // Square buttons of the title bar and toolbar.
  float glyphSize;
  float spacingSmall;
  float spacing;
  float spacingLarge;

  // Typography (sizes in DIPs).
  const wchar_t *fontFamily;
  float fontSizeSmall;
  float fontSizeBody;
  float fontSizeTitle;
};

// Neutral, modern light palette.
inline constexpr ThemeTokens kLightTheme = {
  .background     = 0xFFFFFF,
  .surface        = 0xF4F4F5,
  .field          = 0xFFFFFF,
  .border         = 0xE4E4E7,
  .text           = 0x18181B,
  .mutedText      = 0x71717A,
  .accent         = 0x2563EB,
  .accentHover    = 0x1D4ED8,
  .accentPressed  = 0x1E40AF,
  .accentText     = 0xFFFFFF,
  .hover          = 0xE4E4E7,
  .closeHover     = 0xE81123,
  .closeHoverText = 0xFFFFFF,
  .pressed        = 0xD4D4D8,
  .closePressed   = 0xF1707A,
  .selected       = 0xDBEAFE,
  .selectedHover  = 0xBFDBFE,
  .selectedPressed = 0x93C5FD,
  .selectedText   = 0x1D4ED8,
  .disabledText   = 0xA1A1AA,

  .cornerRadius   = 6.0f,
  .titleBarHeight = 40.0f,
  .toolbarHeight  = 36.0f,
  .buttonSize     = 28.0f,
  .glyphSize      = 16.0f,
  .spacingSmall   = 4.0f,
  .spacing        = 8.0f,
  .spacingLarge   = 16.0f,

  .fontFamily     = L"Segoe UI",
  .fontSizeSmall  = 12.0f,
  .fontSizeBody   = 14.0f,
  .fontSizeTitle  = 16.0f,
};

// Neutral, modern dark palette: the same sizes and typography, other colors.
inline constexpr ThemeTokens kDarkTheme = {
  .background     = 0x18181B,
  .surface        = 0x27272A,
  .field          = 0x27272A,
  .border         = 0x3F3F46,
  .text           = 0xFAFAFA,
  .mutedText      = 0xA1A1AA,
  .accent         = 0x3B82F6,
  .accentHover    = 0x60A5FA,
  .accentPressed  = 0x2563EB,
  .accentText     = 0xFFFFFF,
  .hover          = 0x3F3F46,
  .closeHover     = 0xE81123,
  .closeHoverText = 0xFFFFFF,
  .pressed        = 0x52525B,
  .closePressed   = 0xF1707A,
  .selected       = 0x1E3A8A,
  .selectedHover  = 0x1D4ED8,
  .selectedPressed = 0x2563EB,
  .selectedText   = 0x93C5FD,
  .disabledText   = 0x52525B,

  .cornerRadius   = 6.0f,
  .titleBarHeight = 40.0f,
  .toolbarHeight  = 36.0f,
  .buttonSize     = 28.0f,
  .glyphSize      = 16.0f,
  .spacingSmall   = 4.0f,
  .spacing        = 8.0f,
  .spacingLarge   = 16.0f,

  .fontFamily     = L"Segoe UI",
  .fontSizeSmall  = 12.0f,
  .fontSizeBody   = 14.0f,
  .fontSizeTitle  = 16.0f,
};

#endif
