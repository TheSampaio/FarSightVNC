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

#ifndef _HOME_LAYOUT_H_
#define _HOME_LAYOUT_H_

#include <algorithm>

// Geometry of the scrolling host list of the Home page (HomePage), without
// any window or Direct2D type so that it can be tested. The heading, the
// address field and the action buttons stay fixed; only the favorite and
// recent hosts scroll, inside the list viewport. Values are DIPs; client
// coordinates are those of the page window, list coordinates start at the
// top of the list content (unscrolled).
namespace fsv::shell {

// Vertical span of the list viewport in client coordinates.
struct HomeListGeometry
{
  float top;
  float bottom;

  float height() const { return bottom - top; }
};

// Scroll offset kept in [0, max(0, content - viewport)].
inline float clampScroll(float offset, float content, float viewport)
{
  float maximum = (std::max)(0.0f, content - viewport);
  return (std::max)(0.0f, (std::min)(offset, maximum));
}

// Height of the list viewport: from listTop down to bottomMargin above the
// bottom of the client area, never below minHeight (a short window then
// clips the list instead of moving the fixed part).
inline float listHeight(float clientDips, float listTop, float bottomMargin, float minHeight)
{
  return (std::max)(minHeight, clientDips - bottomMargin - listTop);
}

// Height of the list viewport that is actually visible: listHeight(), but
// never past the bottom of the client area (and never negative). The scroll
// range, the thumb and Page Up/Down use it: with the taller minimum height
// the last rows of a short window could never be scrolled into view.
inline float visibleListHeight(float clientDips, float listTop, float bottomMargin,
                               float minHeight)
{
  float height = listHeight(clientDips, listTop, bottomMargin, minHeight);
  return (std::max)(0.0f, (std::min)(height, clientDips - listTop));
}

// True when client y is inside the list viewport; *listY then receives the
// matching list coordinate at the scroll offset.
inline bool listPoint(const HomeListGeometry &list, float scroll, float clientY, float *listY)
{
  if (clientY < list.top || clientY >= list.bottom) {
    return false;
  }
  *listY = clientY - list.top + scroll;
  return true;
}

// Index of the row (list coordinates, rows[i] = {top, bottom}) under client
// y, or -1 when y is outside the viewport or between rows.
struct HomeRowSpan
{
  float top;
  float bottom;
};

inline int rowAt(const HomeListGeometry &list, float scroll, float clientY,
                 const HomeRowSpan *rows, int count)
{
  float y;
  if (!listPoint(list, scroll, clientY, &y)) {
    return -1;
  }
  for (int i = 0; i < count; i++) {
    if (y >= rows[i].top && y < rows[i].bottom) {
      return i;
    }
  }
  return -1;
}

// Offset that shows the span [top, bottom] (list coordinates) with margin
// around it in a viewport of the given height, moving as little as possible
// from scroll (not clamped: see clampScroll()).
inline float scrollToShow(float scroll, float top, float bottom, float viewport, float margin)
{
  if (top - margin < scroll) {
    return top - margin;
  }
  if (bottom + margin > scroll + viewport) {
    return bottom + margin - viewport;
  }
  return scroll;
}

// Scrollbar thumb along the viewport: false when the content fits (no
// thumb); otherwise *thumbTop (relative to the viewport top) and
// *thumbHeight, at least minThumb.
inline bool scrollThumb(float scroll, float content, float viewport, float minThumb,
                        float *thumbTop, float *thumbHeight)
{
  if (content <= viewport || viewport <= 0.0f) {
    return false;
  }
  float height = (std::max)(minThumb, viewport * viewport / content);
  height = (std::min)(height, viewport);
  float range = content - viewport;
  *thumbHeight = height;
  *thumbTop = (viewport - height) * (clampScroll(scroll, content, viewport) / range);
  return true;
}

} // namespace fsv::shell

#endif
