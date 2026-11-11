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

#ifndef _CHROME_RENDERER_H_
#define _CHROME_RENDERER_H_

#include "util/winhdr.h"

#include <d2d1.h>
#include <dwrite.h>
#include <vector>

#include "ThemeTokens.h"

// Draws the window chrome (title bar, toolbar, pages) of one window with
// Direct2D and DirectWrite. Coordinates and sizes are in DIPs; the render
// target follows the DPI of the window. All calls are made on the window
// thread.
//
// Use: attach() once the window exists, resize() on WM_SIZE, setDpi() on
// WM_DPICHANGED, and in WM_PAINT beginDraw(), the draw calls, endDraw().
class ChromeRenderer
{
public:
  ChromeRenderer();
  ~ChromeRenderer();

  // Binds the renderer to a window. dpi is the DPI of the window
  // (DpiUtils::getDpiForWindow); fontFamily comes from ThemeTokens.
  void attach(HWND hwnd, UINT dpi, const wchar_t *fontFamily = kLightTheme.fontFamily);
  void setDpi(UINT dpi);
  UINT getDpi() const;
  // Client size of the window in pixels.
  void resize(UINT width, UINT height);

  float pixelsToDips(int pixels) const;
  int dipsToPixels(float dips) const;

  // Starts drawing (WM_PAINT). Returns false when Direct2D is unavailable;
  // the caller then paints with GDI and does not call endDraw().
  bool beginDraw();
  // Ends drawing. A lost device drops the render target, which is created
  // again by the next beginDraw(); the window is invalidated for that.
  void endDraw();

  void clear(ThemeColor color);
  void fillRect(const D2D1_RECT_F &rect, ThemeColor color);
  void fillRoundRect(const D2D1_RECT_F &rect, float radius, ThemeColor color);
  void drawRoundRect(const D2D1_RECT_F &rect, float radius, ThemeColor color,
                     float strokeWidth = 1.0f);
  // One line of text, vertically centered in rect, cut with an ellipsis.
  void drawText(const wchar_t *text, const D2D1_RECT_F &rect, ThemeColor color,
                float fontSize, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
  // Draws an icon centered in box. For now the glyph is a short text
  // placeholder; the icon phase draws Lucide SVG documents here when
  // supportsSvgGlyphs() is true and keeps the text as the fallback.
  void drawGlyph(const wchar_t *glyph, const D2D1_RECT_F &box, ThemeColor color);
  // True when the render target is an ID2D1DeviceContext5 (Windows 10 1703
  // or later), which can draw SVG documents.
  bool supportsSvgGlyphs();

  // Text format of the cache for a size (DIPs) and weight, or 0. The cache
  // owns it; it lives as long as the renderer.
  IDWriteTextFormat *getTextFormat(float fontSize, DWRITE_FONT_WEIGHT weight);

  // Factories shared by every renderer of the process, created on first use
  // and kept until exit. 0 when Direct2D or DirectWrite is unavailable.
  static ID2D1Factory *getD2DFactory();
  static IDWriteFactory *getDWriteFactory();

private:
  ChromeRenderer(const ChromeRenderer &);
  ChromeRenderer &operator=(const ChromeRenderer &);

  bool createTarget();
  void discardTarget();
  ID2D1SolidColorBrush *brush(ThemeColor color);

  struct CachedFormat
  {
    int sizeKey;  // Size in 1/100 DIP.
    DWRITE_FONT_WEIGHT weight;
    IDWriteTextFormat *format;
  };
  std::vector<CachedFormat> m_formats;
  IDWriteInlineObject *m_ellipsis;

  HWND m_hwnd;
  UINT m_dpi;
  const wchar_t *m_fontFamily;
  ID2D1HwndRenderTarget *m_target;
  ID2D1SolidColorBrush *m_brush;
  // -1: not checked yet for the current target.
  int m_svgSupport;
};

#endif
