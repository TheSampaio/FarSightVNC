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

// Deterministic test for the geometry of the tab strip of the title bar
// (tvnviewer/shell/TabStripLayout.h): the width shared by the tabs, the "+"
// slot after them, the tabs that fit when the strip is short (always
// including the active one) and the hit tests of a tab and of its close
// button. No window is created.

#include "tvnviewer/shell/TabStripLayout.h"

namespace {

bool closeTo(float a, float b)
{
  float d = a - b;
  return d < 0.001f && d > -0.001f;
}

float widthOf(const TabStripLayout &strip, size_t index)
{
  return strip.tab(index).right - strip.tab(index).left;
}

size_t visibleCount(const TabStripLayout &strip)
{
  size_t count = 0;
  for (size_t i = 0; i < strip.count(); i++) {
    if (strip.isVisible(i)) {
      count++;
    }
  }
  return count;
}

}

// Called by main() of FavoritesTest.cpp with its check().
void testTabStripLayout(void (*check)(bool, const char *))
{
  const size_t none = TabStripLayout::npos;
  TabStripLayout strip;

  // No tab: the "+" button sits at the start of the strip.
  strip.layout(100.0f, 800.0f, 6.0f, 34.0f, 0, none);
  check(strip.count() == 0, "an empty strip has no tab");
  check(closeTo(strip.newTabSlot().left, 100.0f) &&
        closeTo(strip.newTabSlot().right - strip.newTabSlot().left, TabStripLayout::kNewTabWidth),
        "the plus button of an empty strip is at its start");

  // One tab in a wide strip is as wide as the maximum.
  strip.layout(100.0f, 800.0f, 6.0f, 34.0f, 1, 0);
  check(strip.count() == 1 && strip.isVisible(0), "one tab is shown");
  check(closeTo(widthOf(strip, 0), TabStripLayout::kTabMaxWidth), "a lone tab takes the maximum width");
  check(closeTo(strip.tab(0).left, 100.0f), "the first tab starts at the left of the strip");
  check(closeTo(strip.newTabSlot().left, strip.tab(0).right + TabStripLayout::kTabGap),
        "the plus button follows the last tab");
  check(closeTo(strip.tab(0).top, 6.0f) && closeTo(strip.tab(0).bottom, 34.0f),
        "tabs span the height given");

  // Several tabs of a wide strip stay at the maximum width, side by side.
  strip.layout(100.0f, 1400.0f, 6.0f, 34.0f, 3, 1);
  check(visibleCount(strip) == 3, "three tabs fit a wide strip");
  check(closeTo(widthOf(strip, 0), TabStripLayout::kTabMaxWidth) &&
        closeTo(widthOf(strip, 2), TabStripLayout::kTabMaxWidth),
        "tabs keep the maximum width while there is room");
  check(closeTo(strip.tab(1).left, strip.tab(0).right + TabStripLayout::kTabGap),
        "tabs are separated by the gap");

  // In a short strip all the tabs shrink together, the plus button still fits.
  strip.layout(100.0f, 500.0f, 6.0f, 34.0f, 5, 0);
  check(visibleCount(strip) == 5, "five tabs fit a 400 wide strip");
  check(closeTo(widthOf(strip, 0), widthOf(strip, 4)), "the tabs shrink to the same width");
  check(widthOf(strip, 0) < TabStripLayout::kTabMaxWidth &&
        widthOf(strip, 0) >= TabStripLayout::kTabMinWidth,
        "the shrunk width stays between the minimum and the maximum");
  check(strip.tab(4).right <= strip.newTabSlot().left, "the last tab ends before the plus button");
  check(strip.newTabSlot().right <= 500.0f + 0.001f, "the plus button stays inside the strip");

  // Too many tabs: only the ones that fit are shown, around the active tab.
  strip.layout(100.0f, 500.0f, 6.0f, 34.0f, 10, 0);
  check(visibleCount(strip) < 10 && visibleCount(strip) > 0, "not every tab of a crowded strip is shown");
  size_t shown = visibleCount(strip);
  check(strip.isVisible(0) && !strip.isVisible(9), "the first tabs are shown for the first active tab");
  check(widthOf(strip, 0) >= TabStripLayout::kTabMinWidth - 0.001f,
        "a shown tab is never narrower than the minimum");
  check(strip.newTabSlot().right <= 500.0f + 0.001f && strip.newTabSlot().right > strip.newTabSlot().left,
        "the plus button of a crowded strip is shown");
  strip.layout(100.0f, 500.0f, 6.0f, 34.0f, 10, 9);
  check(visibleCount(strip) == shown, "the same number of tabs is shown for another active tab");
  check(strip.isVisible(9) && strip.isVisible(10 - shown) && !strip.isVisible(10 - shown - 1),
        "the window of tabs slides to include the last tab");
  strip.layout(100.0f, 500.0f, 6.0f, 34.0f, 10, 7);
  check(strip.isVisible(7) && strip.isVisible(7 + 1 - shown) && !strip.isVisible(7 - shown),
        "the window of tabs slides just far enough to include the active tab");

  // A strip with no room for a tab shows none; the plus button needs room too.
  strip.layout(100.0f, 110.0f, 6.0f, 34.0f, 3, 0);
  check(visibleCount(strip) == 0, "a strip of 10 shows no tab");
  check(closeTo(strip.newTabSlot().right, strip.newTabSlot().left), "a strip too short has no plus button");

  // Hit tests.
  strip.layout(100.0f, 1400.0f, 6.0f, 34.0f, 3, 0);
  float middle = (strip.tab(1).left + strip.tab(1).right) / 2.0f;
  float y = 20.0f;
  check(strip.tabAt(middle, y) == 1, "a point inside the second tab is on it");
  check(strip.tabAt(strip.tab(0).right + TabStripLayout::kTabGap / 2.0f, y) == none,
        "a point in the gap between tabs is on none");
  check(strip.tabAt(middle, 2.0f) == none && strip.tabAt(middle, 40.0f) == none,
        "a point above or below the tabs is on none");
  check(strip.tabAt(strip.newTabSlot().left + 4.0f, y) == none, "the plus button is not a tab");
  check(strip.tabAt(strip.tab(2).left, y) == 2 && strip.tabAt(strip.tab(2).right, y) == none,
        "a tab includes its left edge and not its right edge");

  const D2D1_RECT_F close = strip.closeButton(1);
  check(close.right <= strip.tab(1).right && close.left > strip.tab(1).left,
        "the close button is inside its tab");
  check(closeTo(close.right, strip.tab(1).right - TabStripLayout::kCloseMargin),
        "the close button keeps its margin at the right edge");
  check(closeTo((close.top + close.bottom) / 2.0f, 20.0f), "the close button is centered vertically");
  check(strip.isOnClose(1, (close.left + close.right) / 2.0f, y), "the center of the close button is on it");
  check(!strip.isOnClose(1, strip.tab(1).left + 5.0f, y), "the start of the tab is not the close button");
  check(!strip.isOnClose(0, (close.left + close.right) / 2.0f, y),
        "the close button of one tab is not the close button of another");
  check(!strip.isOnClose(7, middle, y), "a tab that does not exist has no close button");

  strip.layout(100.0f, 500.0f, 6.0f, 34.0f, 10, 0);
  check(!strip.isOnClose(9, 0.0f, 0.0f) && strip.tabAt(strip.tab(9).left, y) == none,
        "a tab left out is neither hit nor closable");
}
