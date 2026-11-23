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

#ifndef _TAB_STRIP_LAYOUT_H_
#define _TAB_STRIP_LAYOUT_H_

#include "util/winhdr.h"

#include <d2d1.h>

#include <cmath>
#include <cstddef>
#include <vector>

// Places the tabs of the title bar side by side, like the tabs of a browser:
// each one as wide as possible up to a maximum, all shrinking together when
// the strip is short, followed by the slot of the "+" button. When even the
// minimum width does not fit all of them, only as many tabs as fit are laid
// out (the others get an empty rectangle, so they are never drawn or hit),
// and they always include the active tab. Coordinates in DIPs. It has no
// window or drawing in it, so that it can be tested.
class TabStripLayout
{
public:
  static constexpr float kTabMaxWidth = 240.0f;
  // A tab at this width still shows its close button and a few letters.
  static constexpr float kTabMinWidth = 64.0f;
  static constexpr float kTabGap = 4.0f;
  static constexpr float kNewTabWidth = 28.0f;
  static constexpr float kCloseSize = 18.0f;
  // Space between the close button and the right edge of its tab.
  static constexpr float kCloseMargin = 6.0f;
  static constexpr size_t npos = static_cast<size_t>(-1);

  TabStripLayout() : m_newTab(D2D1::RectF()) {}

  // Lays count tabs out between left and right, from top to bottom; active
  // is the index of the tab that must stay visible (npos: none).
  void layout(float left, float right, float top, float bottom, size_t count, size_t active)
  {
    m_tabs.clear();
    float room = right - left - kNewTabWidth;
    size_t capacity = 0;
    if (room > 0.0f) {
      capacity = static_cast<size_t>(std::floor(room / (kTabMinWidth + kTabGap)));
    }
    size_t shown = count < capacity ? count : capacity;
    // The window of visible tabs slides to the right just far enough to
    // include the active tab.
    size_t first = 0;
    if (shown < count && active != npos && active >= shown) {
      first = active + 1 - shown;
    }
    float width = 0.0f;
    if (shown > 0) {
      width = (room - kTabGap * shown) / shown;
      if (width > kTabMaxWidth) {
        width = kTabMaxWidth;
      }
    }
    float x = left;
    for (size_t i = 0; i < count; i++) {
      if (i >= first && i < first + shown) {
        m_tabs.push_back(D2D1::RectF(x, top, x + width, bottom));
        x += width + kTabGap;
      } else {
        m_tabs.push_back(D2D1::RectF(x, top, x, bottom));
      }
    }
    m_newTab = D2D1::RectF(x, top, x + kNewTabWidth, bottom);
    if (m_newTab.right > right) {
      m_newTab.right = m_newTab.left;
    }
  }

  size_t count() const { return m_tabs.size(); }
  const D2D1_RECT_F &tab(size_t index) const { return m_tabs[index]; }
  // False for a tab that is not laid out (no room).
  bool isVisible(size_t index) const { return m_tabs[index].right > m_tabs[index].left; }
  // Close button of a tab, at its right edge, centered vertically.
  D2D1_RECT_F closeButton(size_t index) const
  {
    const D2D1_RECT_F &rect = m_tabs[index];
    float cy = (rect.top + rect.bottom) / 2.0f;
    return D2D1::RectF(rect.right - kCloseMargin - kCloseSize, cy - kCloseSize / 2.0f,
                       rect.right - kCloseMargin, cy + kCloseSize / 2.0f);
  }
  // "+" button; empty (zero width) when the strip is too short for it.
  const D2D1_RECT_F &newTabSlot() const { return m_newTab; }

  // Tab under the point, or npos.
  size_t tabAt(float x, float y) const
  {
    for (size_t i = 0; i < m_tabs.size(); i++) {
      if (contains(m_tabs[i], x, y)) {
        return i;
      }
    }
    return npos;
  }
  // The point is on the close button of tab index.
  bool isOnClose(size_t index, float x, float y) const
  {
    return index < m_tabs.size() && isVisible(index) && contains(closeButton(index), x, y);
  }

  static bool contains(const D2D1_RECT_F &rect, float x, float y)
  {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
  }

private:
  std::vector<D2D1_RECT_F> m_tabs;
  D2D1_RECT_F m_newTab;
};

#endif
