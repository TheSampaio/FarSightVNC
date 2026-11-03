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

#ifndef _FULL_SCREEN_TOOLBAR_LOGIC_H_
#define _FULL_SCREEN_TOOLBAR_LOGIC_H_

// Auto-hide decisions of the floating full screen toolbar, without any window
// so that they can be tested: the toolbar is shown when the pointer touches a
// thin strip at the top edge of a monitor, kept visible while the pointer is
// near it or while it is pinned, and hidden after a delay without the pointer
// near it. Times are milliseconds of a wrapping 32-bit tick counter
// (GetTickCount()); coordinates are screen pixels.
class FullScreenToolbarLogic
{
public:
  // Rectangle with exclusive right and bottom edges, like RECT.
  struct Box
  {
    int left, top, right, bottom;
  };

  static const unsigned int DEFAULT_HIDE_DELAY_MS = 2000;

  explicit FullScreenToolbarLogic(unsigned int hideDelayMs = DEFAULT_HIDE_DELAY_MS)
  : m_hideDelay(hideDelayMs), m_visible(false), m_pinned(false), m_lastActivity(0)
  {
  }

  // True if (x, y) is in the box enlarged by margin on every side.
  static bool isNear(const Box &box, int margin, int x, int y)
  {
    return x >= box.left - margin && x < box.right + margin &&
           y >= box.top - margin && y < box.bottom + margin;
  }

  // True if (x, y) is in the strip of stripHeight pixels at the top edge of
  // the monitor.
  static bool isInHotStrip(const Box &monitor, int stripHeight, int x, int y)
  {
    return x >= monitor.left && x < monitor.right &&
           y >= monitor.top && y < monitor.top + stripHeight;
  }

  // Makes the toolbar visible (on entering full screen or from the hot strip).
  void show(unsigned int now)
  {
    m_visible = true;
    m_lastActivity = now;
  }

  void hide()
  {
    m_visible = false;
  }

  // The pointer moved over the remote desktop. monitor is the monitor under
  // the pointer and toolbar the current toolbar rectangle. Returns true if the
  // toolbar must be shown on that monitor.
  bool onPointerMove(const Box &monitor, int stripHeight,
                     const Box &toolbar, int nearMargin,
                     int x, int y, unsigned int now)
  {
    if (isInHotStrip(monitor, stripHeight, x, y)) {
      show(now);
      return true;
    }
    if (m_visible && isNear(toolbar, nearMargin, x, y)) {
      m_lastActivity = now;
    }
    return false;
  }

  // Periodic check; pointerNear tells whether the pointer is over or near the
  // toolbar. Returns true if the toolbar must be hidden now.
  bool onTimer(bool pointerNear, unsigned int now)
  {
    if (!m_visible) {
      return false;
    }
    if (m_pinned || pointerNear) {
      m_lastActivity = now;
      return false;
    }
    // Unsigned subtraction is correct across the wrap of the tick counter.
    if (now - m_lastActivity < m_hideDelay) {
      return false;
    }
    m_visible = false;
    return true;
  }

  // A pinned toolbar stays visible; after unpinning the delay starts again.
  void setPinned(bool pinned, unsigned int now)
  {
    m_pinned = pinned;
    m_lastActivity = now;
  }

  bool isPinned() const { return m_pinned; }
  bool isVisible() const { return m_visible; }

private:
  unsigned int m_hideDelay;
  bool m_visible;
  bool m_pinned;
  unsigned int m_lastActivity;
};

#endif
