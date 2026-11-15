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


// Deterministic test for the geometry of the scrolling host list of the Home
// page (tvnviewer/shell/HomeLayout.h): scroll clamping, the viewport height
// in short windows, hit testing of the rows at a scroll offset, keeping a row
// visible and the scrollbar thumb. No window is created.

#include "tvnviewer/shell/HomeLayout.h"

using fsv::shell::HomeListGeometry;
using fsv::shell::HomeRowSpan;

namespace {

bool closeTo(float a, float b)
{
  float d = a - b;
  return d < 0.001f && d > -0.001f;
}

}

// Called by main() of FavoritesTest.cpp with its check().
void testHomeLayout(void (*check)(bool, const char *))
{
  // Clamp: content 500 in a viewport of 200 scrolls from 0 to 300.
  check(closeTo(fsv::shell::clampScroll(-40.0f, 500.0f, 200.0f), 0.0f), "scroll clamps at 0");
  check(closeTo(fsv::shell::clampScroll(120.0f, 500.0f, 200.0f), 120.0f), "scroll inside range kept");
  check(closeTo(fsv::shell::clampScroll(900.0f, 500.0f, 200.0f), 300.0f), "scroll clamps at max");
  check(closeTo(fsv::shell::clampScroll(50.0f, 150.0f, 200.0f), 0.0f),
        "content shorter than viewport never scrolls");
  check(closeTo(fsv::shell::clampScroll(50.0f, 200.0f, 200.0f), 0.0f),
        "content equal to viewport never scrolls");

  // Viewport height: client 600, list from 250, bottom margin 32.
  check(closeTo(fsv::shell::listHeight(600.0f, 250.0f, 32.0f, 140.0f), 318.0f),
        "list fills down to the bottom margin");
  check(closeTo(fsv::shell::listHeight(300.0f, 250.0f, 32.0f, 140.0f), 140.0f),
        "short window keeps the minimum list height");
  check(closeTo(fsv::shell::listHeight(100.0f, 250.0f, 32.0f, 140.0f), 140.0f),
        "window shorter than the fixed part keeps the minimum");

  // Visible viewport: the minimum height is cut at the bottom of the client.
  check(closeTo(fsv::shell::visibleListHeight(600.0f, 250.0f, 32.0f, 140.0f), 318.0f),
        "visible list fills down to the bottom margin");
  check(closeTo(fsv::shell::visibleListHeight(300.0f, 250.0f, 32.0f, 140.0f), 50.0f),
        "short window: visible list ends at the client bottom");
  check(closeTo(fsv::shell::visibleListHeight(400.0f, 250.0f, 32.0f, 140.0f), 140.0f),
        "minimum height kept while it fits in the client");
  check(closeTo(fsv::shell::visibleListHeight(100.0f, 250.0f, 32.0f, 140.0f), 0.0f),
        "window shorter than the fixed part has no visible list");
  {
    // Short window, content 500: the last row must be reachable, so the
    // scroll range follows the visible 50, not the minimum 140.
    float visible = fsv::shell::visibleListHeight(300.0f, 250.0f, 32.0f, 140.0f);
    check(closeTo(fsv::shell::clampScroll(1000.0f, 500.0f, visible), 450.0f),
          "short window scrolls to the last row");
    float top = -1.0f;
    float height = -1.0f;
    check(fsv::shell::scrollThumb(450.0f, 500.0f, visible, 24.0f, &top, &height) &&
          closeTo(height, 24.0f) && closeTo(top + height, visible),
          "short window thumb ends at the visible bottom");
    check(!fsv::shell::scrollThumb(0.0f, 500.0f, 0.0f, 24.0f, &top, &height),
          "no thumb without a visible list");
  }

  // Rows in list coordinates: header 0-28, rows 28-84 and 86-142, 144-200.
  HomeListGeometry list = { 250.0f, 400.0f };
  HomeRowSpan rows[] = { { 28.0f, 84.0f }, { 86.0f, 142.0f }, { 144.0f, 200.0f } };
  check(closeTo(list.height(), 150.0f), "list height");
  check(fsv::shell::rowAt(list, 0.0f, 250.0f + 30.0f, rows, 3) == 0, "first row without scroll");
  check(fsv::shell::rowAt(list, 0.0f, 250.0f + 10.0f, rows, 3) == -1, "header is not a row");
  check(fsv::shell::rowAt(list, 0.0f, 250.0f + 85.0f, rows, 3) == -1, "gap between rows");
  check(fsv::shell::rowAt(list, 60.0f, 250.0f + 30.0f, rows, 3) == 1, "row hit with offset");
  check(fsv::shell::rowAt(list, 60.0f, 399.0f, rows, 3) == -1, "beyond the last row");
  check(fsv::shell::rowAt(list, 60.0f, 249.0f, rows, 3) == -1,
        "point above the list never hits (fixed part)");
  check(fsv::shell::rowAt(list, 0.0f, 400.0f, rows, 3) == -1,
        "point below the list never hits (clipped row)");
  check(fsv::shell::rowAt(list, 100.0f, 200.0f, rows, 3) == -1,
        "scrolled row under the fixed part never hits");
  float listY = -1.0f;
  check(fsv::shell::listPoint(list, 40.0f, 260.0f, &listY) && closeTo(listY, 50.0f),
        "list coordinate of a client point");
  check(!fsv::shell::listPoint(list, 40.0f, 401.0f, &listY), "client point outside the list");

  // Keeping a row visible in a viewport of 150 with a margin of 8.
  check(closeTo(fsv::shell::scrollToShow(0.0f, 28.0f, 84.0f, 150.0f, 8.0f), 0.0f),
        "visible row does not scroll");
  check(closeTo(fsv::shell::scrollToShow(0.0f, 144.0f, 200.0f, 150.0f, 8.0f), 58.0f),
        "row below scrolls down to its bottom");
  check(closeTo(fsv::shell::scrollToShow(100.0f, 28.0f, 84.0f, 150.0f, 8.0f), 20.0f),
        "row above scrolls up to its top");

  // Scrollbar thumb.
  float thumbTop = -1.0f;
  float thumbHeight = -1.0f;
  check(!fsv::shell::scrollThumb(0.0f, 150.0f, 200.0f, 24.0f, &thumbTop, &thumbHeight),
        "no thumb when the content fits");
  check(fsv::shell::scrollThumb(0.0f, 400.0f, 200.0f, 24.0f, &thumbTop, &thumbHeight) &&
        closeTo(thumbTop, 0.0f) && closeTo(thumbHeight, 100.0f), "thumb at the top");
  check(fsv::shell::scrollThumb(200.0f, 400.0f, 200.0f, 24.0f, &thumbTop, &thumbHeight) &&
        closeTo(thumbTop, 100.0f) && closeTo(thumbHeight, 100.0f), "thumb at the bottom");
  check(fsv::shell::scrollThumb(0.0f, 100000.0f, 200.0f, 24.0f, &thumbTop, &thumbHeight) &&
        closeTo(thumbHeight, 24.0f), "thumb keeps its minimum height");
}
