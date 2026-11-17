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

  // Pinned favorites: header 28, rows of 56 + 2 gap = step 58, section gap 28
  // (26 after the row gap), bottom margin 32, minimum list 140.
  const float header = 28.0f;
  const float step = 58.0f;
  const float gapAfter = 26.0f;
  check(closeTo(fsv::shell::pinnedBudget(900.0f, 250.0f, gapAfter, 32.0f, 140.0f), 452.0f),
        "pinned budget leaves the minimum list below the pinned block");
  check(fsv::shell::pinnedBudget(400.0f, 250.0f, gapAfter, 32.0f, 140.0f) < 0.0f,
        "short window has a negative pinned budget");
  check(closeTo(fsv::shell::pinnedHeight(3, header, step), 202.0f), "pinned height of 3 rows");
  check(closeTo(fsv::shell::pinnedHeight(0, header, step), 0.0f), "no pinned rows, no height");
  check(fsv::shell::pinnedRowCount(452.0f, 3, header, step) == 3, "few favorites all pinned");
  check(fsv::shell::pinnedRowCount(452.0f, 0, header, step) == 0, "no favorites, nothing pinned");
  check(fsv::shell::pinnedRowCount(452.0f, 64, header, step) == 7,
        "many favorites: only the rows that fit are pinned");
  check(fsv::shell::pinnedRowCount(28.0f + 58.0f, 5, header, step) == 1,
        "budget of the header and one row pins one row");
  check(fsv::shell::pinnedRowCount(28.0f + 58.0f - 1.0f, 5, header, step) == 0,
        "budget below the header and one row pins nothing");
  check(fsv::shell::pinnedRowCount(-120.0f, 5, header, step) == 0,
        "negative budget pins nothing");
  {
    // Pinned rows never overflow the window and keep the minimum list.
    float clientDips = 600.0f;
    float pinnedTop = 250.0f;
    int pinned = fsv::shell::pinnedRowCount(
      fsv::shell::pinnedBudget(clientDips, pinnedTop, gapAfter, 32.0f, 140.0f), 64, header, step);
    float listTop = pinnedTop + fsv::shell::pinnedHeight(pinned, header, step) + gapAfter;
    check(pinned == 2 && listTop + 140.0f <= clientDips - 32.0f,
          "pinned block leaves the minimum list above the bottom margin");

    // No favorites: the list starts where the pinned block would have, with
    // no gap, so the layout is that of a list of recents only.
    check(closeTo(pinnedTop + fsv::shell::pinnedHeight(0, header, step), pinnedTop),
          "no favorites leaves no gap above the list");

    // Scroll range of the list below the pinned block: only the recents
    // (header 28, 10 rows) scroll, in the height that is left.
    float viewport = fsv::shell::visibleListHeight(clientDips, listTop, 32.0f, 140.0f);
    float content = header + 10.0f * step;
    check(closeTo(viewport, 148.0f) && viewport >= 140.0f,
          "list below the pinned block gets the rest of the window, at least the minimum");
    check(closeTo(fsv::shell::clampScroll(9999.0f, content, viewport), content - viewport),
          "scroll range counts only the list below the pinned block");

    // Hit test: the pinned rows in page coordinates (scroll 0), then the
    // scrolled rows in the list below them.
    HomeListGeometry pinnedArea = { pinnedTop, pinnedTop + fsv::shell::pinnedHeight(pinned, header, step) };
    // The pinned block is a list that never scrolls (offset 0); its rows are
    // relative to its top.
    HomeRowSpan pinnedRows[] = { { header, header + 56.0f },
                                 { header + step, header + step + 56.0f } };
    check(fsv::shell::rowAt(pinnedArea, 0.0f, pinnedTop + header + 10.0f, pinnedRows, 2) == 0,
          "first pinned row");
    check(fsv::shell::rowAt(pinnedArea, 0.0f, pinnedTop + header + step + 10.0f, pinnedRows, 2) == 1,
          "second pinned row");
    check(fsv::shell::rowAt(pinnedArea, 0.0f, pinnedTop + 10.0f, pinnedRows, 2) == -1,
          "pinned header is not a row");
    check(fsv::shell::rowAt(pinnedArea, 0.0f, pinnedArea.bottom + 1.0f, pinnedRows, 2) == -1,
          "gap under the pinned block is not a pinned row");

    HomeListGeometry recents = { listTop, listTop + viewport };
    HomeRowSpan recentRows[] = { { header, header + 56.0f }, { header + step, header + step + 56.0f },
                                 { header + 2.0f * step, header + 2.0f * step + 56.0f } };
    check(fsv::shell::rowAt(recents, 0.0f, listTop + header + 10.0f, recentRows, 3) == 0,
          "first recent row below the pinned block");
    check(fsv::shell::rowAt(recents, step, listTop + header + 10.0f, recentRows, 3) == 1,
          "scrolled recent row below the pinned block");
    check(fsv::shell::rowAt(recents, step, pinnedArea.bottom - 5.0f, recentRows, 3) == -1,
          "a pinned row's point never hits a scrolled recent row");
    float listY = -1.0f;
    check(!fsv::shell::listPoint(recents, 40.0f, pinnedTop + header + 10.0f, &listY),
          "pinned area is outside the scrolled list");
    check(fsv::shell::listPoint(recents, 40.0f, listTop + 10.0f, &listY) && closeTo(listY, 50.0f),
          "list coordinate below the pinned block");
  }
  {
    // Short window: nothing fits pinned, the list takes the favorites and
    // keeps the old behavior (list starts under the buttons).
    float pinnedTop = 250.0f;
    int pinned = fsv::shell::pinnedRowCount(
      fsv::shell::pinnedBudget(400.0f, pinnedTop, gapAfter, 32.0f, 140.0f), 5, header, step);
    check(pinned == 0, "short window pins no favorite");
    check(closeTo(fsv::shell::visibleListHeight(400.0f, pinnedTop, 32.0f, 140.0f), 140.0f),
          "short window keeps the list below the buttons");
  }
}
