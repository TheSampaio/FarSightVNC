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

#ifndef _SVG_ICONS_H_
#define _SVG_ICONS_H_

// No util/winhdr.h: SvgIcons.cpp includes this header under a newer WINVER.
#include <windows.h>

#include <d2d1.h>
#include <vector>

#include "ThemeTokens.h"

// Declared in d2d1_3.h (Windows 10 1703), which does not compile under the
// WINVER 0x0501 of util/winhdr.h: only SvgIcons.cpp includes it.
struct ID2D1DeviceContext5;
struct ID2D1SvgDocument;

// Draws the Lucide icons embedded as RCDATA resources (vendor/lucide/) with
// ID2D1DeviceContext5 (Windows 10 1703 or later). One cache belongs to one
// render target: the parsed documents are resources of its device, so
// ChromeRenderer releases the cache with the target.
//
// Each icon is parsed once per color: stroke="currentColor" of the Lucide
// file is replaced with the color before parsing. The document keeps the
// 24x24 viewBox of Lucide and is scaled to the box it is drawn in, in DIPs,
// so it follows the DPI of the target.
class SvgIconCache
{
public:
  SvgIconCache();
  ~SvgIconCache();

  // Binds the cache to target. Returns false (and draws nothing later) when
  // the target is not an ID2D1DeviceContext5.
  bool attach(ID2D1RenderTarget *target);
  // Releases the documents and the device context.
  void release();

  // Draws the icon of resource resourceId centered in box, as large as the
  // square that fits in it. Returns false when it cannot draw (no device
  // context, missing resource, parse error); the caller draws a fallback.
  bool draw(int resourceId, const D2D1_RECT_F &box, ThemeColor color);

private:
  SvgIconCache(const SvgIconCache &);
  SvgIconCache &operator=(const SvgIconCache &);

  ID2D1SvgDocument *document(int resourceId, ThemeColor color);

  struct Entry
  {
    int resourceId;
    ThemeColor color;
    // 0 when the icon could not be parsed (not tried again).
    ID2D1SvgDocument *document;
  };
  std::vector<Entry> m_entries;
  ID2D1DeviceContext5 *m_context;
};

#endif
