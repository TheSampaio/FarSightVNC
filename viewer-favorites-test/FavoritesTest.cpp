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

// Modified for FarSightVNC on 2026-11-03: also covers the auto-hide decisions
// of the full screen toolbar (FullScreenToolbarLogic).

// Deterministic test for the in-memory logic of ConnectionFavorites (the
// favorite and recent hosts of the login dialog): case-insensitive host
// comparison, deduplication, ordering and limits. The registry is never
// touched: load() and save() are not called.

#include "client-config-lib/ConnectionFavorites.h"
#include "tvnviewer/FullScreenToolbarLogic.h"

#include <stdio.h>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool ok, const char *what)
{
  if (ok) {
    g_passed++;
  } else {
    g_failed++;
    printf("FAIL: %s\n", what);
  }
}

bool same(const StringStorage &s, const TCHAR *expected)
{
  return _tcscmp(s.getString(), expected) == 0;
}

std::vector<StringStorage> hosts(const TCHAR *const *list, size_t count)
{
  std::vector<StringStorage> result;
  for (size_t i = 0; i < count; i++) {
    result.push_back(StringStorage(list[i]));
  }
  return result;
}

void testHostCompare()
{
  check(ConnectionFavorites::isSameHost(_T("nds-004"), _T("NDS-004")), "compare ignores case");
  check(ConnectionFavorites::isSameHost(_T("Host::5901"), _T("host::5901")), "compare ignores case with port");
  check(!ConnectionFavorites::isSameHost(_T("host::5901"), _T("host::5902")), "different ports differ");
  check(!ConnectionFavorites::isSameHost(_T("host"), _T("host2")), "prefix is not equal");
  check(!ConnectionFavorites::isSameHost(_T(""), _T("")), "empty hosts never match");
  check(!ConnectionFavorites::isSameHost(0, _T("a")), "null host never matches");
}

void testAddRemove()
{
  ConnectionFavorites f;
  check(f.getCount() == 0, "starts empty");
  check(f.getLimit() == ConnectionFavorites::DEFAULT_LIMIT, "default limit");
  check(f.add(_T("alpha")), "add alpha");
  check(f.add(_T("beta::5902")), "add beta");
  check(!f.add(_T("ALPHA")), "duplicate in another case is rejected");
  check(!f.add(_T("")), "empty host is rejected");
  check(!f.add(0), "null host is rejected");
  check(f.getCount() == 2, "two favorites");
  check(_tcscmp(f.getHost(0), _T("alpha")) == 0, "insertion order kept (0)");
  check(_tcscmp(f.getHost(1), _T("beta::5902")) == 0, "insertion order kept (1)");
  check(f.contains(_T("Beta::5902")), "contains ignores case");
  check(!f.contains(_T("beta")), "contains needs the same port");
  check(f.remove(_T("ALPHA")), "remove ignores case");
  check(!f.remove(_T("alpha")), "second remove fails");
  check(f.getCount() == 1 && _tcscmp(f.getHost(0), _T("beta::5902")) == 0, "remaining favorite");
}

void testLimit()
{
  ConnectionFavorites f(2);
  check(f.add(_T("a")) && f.add(_T("b")), "fill up to the limit");
  check(!f.add(_T("c")), "add beyond the limit fails");
  check(f.getCount() == 2, "count stays at the limit");
  check(f.remove(_T("a")) && f.add(_T("c")), "room after a removal");
}

void testHostList()
{
  ConnectionFavorites f;
  f.add(_T("fav-b"));
  f.add(_T("NDS-004"));

  // Recents as stored by ConnectionHistory: most recent first, may contain
  // case variants of the same host.
  const TCHAR *const recentList[] = {
    _T("nds-004"), _T("aqwplo135"), _T("Aqwplo135"), _T("nds-005"),
    _T(""), _T("127.0.0.1::5931"), _T("fav-B")
  };
  std::vector<StringStorage> recents = hosts(recentList, 7);
  std::vector<StringStorage> list;

  size_t favCount = f.buildHostList(recents, 32, &list);
  check(favCount == 2, "favorite count returned");
  check(list.size() == 5, "favorites + unique recents");
  check(list.size() == 5 && same(list[0], _T("fav-b")) && same(list[1], _T("NDS-004")),
        "favorites first, in their order");
  check(list.size() == 5 && same(list[2], _T("aqwplo135")) && same(list[3], _T("nds-005")) &&
        same(list[4], _T("127.0.0.1::5931")),
        "recents follow, most recent first, without favorites, duplicates and empty hosts");

  f.buildHostList(recents, 2, &list);
  check(list.size() == 4 && same(list[2], _T("aqwplo135")) && same(list[3], _T("nds-005")),
        "recent limit counts listed recents only");

  f.buildHostList(recents, 0, &list);
  check(list.size() == 2, "recent limit 0 keeps the favorites only");

  ConnectionFavorites empty;
  favCount = empty.buildHostList(recents, 32, &list);
  check(favCount == 0 && list.size() == 5 && same(list[0], _T("nds-004")) &&
        same(list[2], _T("nds-005")),
        "without favorites the recents keep their order");

  std::vector<StringStorage> none;
  favCount = f.buildHostList(none, 32, &list);
  check(favCount == 2 && list.size() == 2, "without recents only favorites");
}

typedef FullScreenToolbarLogic::Box Box;

void testToolbarGeometry()
{
  Box monitor = { -1920, 0, 0, 1080 };
  check(FullScreenToolbarLogic::isInHotStrip(monitor, 3, -1000, 0), "hot strip: top row");
  check(FullScreenToolbarLogic::isInHotStrip(monitor, 3, -1000, 2), "hot strip: last row of the strip");
  check(!FullScreenToolbarLogic::isInHotStrip(monitor, 3, -1000, 3), "hot strip: below the strip");
  check(FullScreenToolbarLogic::isInHotStrip(monitor, 3, -1920, 0) &&
        !FullScreenToolbarLogic::isInHotStrip(monitor, 3, 0, 0),
        "hot strip: left edge inside, right edge (exclusive) outside");
  Box lower = { 0, 200, 1920, 1280 };
  check(!FullScreenToolbarLogic::isInHotStrip(lower, 3, 100, 0) &&
        FullScreenToolbarLogic::isInHotStrip(lower, 3, 100, 201),
        "hot strip: follows the top of a lower monitor");

  Box bar = { 860, 0, 1060, 30 };
  check(FullScreenToolbarLogic::isNear(bar, 0, 860, 0) && !FullScreenToolbarLogic::isNear(bar, 0, 1060, 0),
        "near: exclusive right edge without margin");
  check(FullScreenToolbarLogic::isNear(bar, 24, 1083, 53) && !FullScreenToolbarLogic::isNear(bar, 24, 1084, 10),
        "near: margin on every side");
}

void testToolbarAutoHide()
{
  Box monitor = { 0, 0, 1920, 1080 };
  Box bar = { 860, 0, 1060, 30 };
  FullScreenToolbarLogic logic(2000);
  check(!logic.isVisible() && !logic.onTimer(false, 100), "hidden at start, timer does nothing");

  logic.show(1000);
  check(!logic.onTimer(false, 2999) && logic.isVisible(), "visible until the delay expires");
  check(logic.onTimer(false, 3000) && !logic.isVisible(), "hidden when the delay expires");
  check(!logic.onTimer(false, 9000), "no second hide request");

  check(!logic.onPointerMove(monitor, 3, bar, 24, 500, 500, 10000) && !logic.isVisible(),
        "move outside the strip does not show it");
  check(logic.onPointerMove(monitor, 3, bar, 24, 500, 0, 10000) && logic.isVisible(),
        "hot strip shows it");
  check(!logic.onTimer(true, 13000) && logic.isVisible(), "pointer near keeps it visible");
  check(!logic.onTimer(false, 14999), "delay restarts when the pointer leaves");
  check(logic.onTimer(false, 15000), "hidden 2 s after the pointer left");

  logic.show(20000);
  check(!logic.onPointerMove(monitor, 3, bar, 24, 900, 40, 21500), "move near the toolbar is not a show request");
  check(!logic.onTimer(false, 23000), "move near the toolbar restarts the delay");
  check(logic.onTimer(false, 23500), "hidden after the restarted delay");
  logic.show(30000);
  logic.onPointerMove(monitor, 3, bar, 24, 900, 200, 31500);
  check(logic.onTimer(false, 32000), "move far from the toolbar does not keep it");
}

void testToolbarPin()
{
  FullScreenToolbarLogic logic(2000);
  logic.show(0);
  logic.setPinned(true, 100);
  check(logic.isPinned() && !logic.onTimer(false, 60000) && logic.isVisible(), "pinned stays visible");
  logic.setPinned(false, 60000);
  check(!logic.onTimer(false, 61999), "unpinned: full delay before hiding");
  check(logic.onTimer(false, 62000) && !logic.isVisible(), "unpinned: hidden after the delay");
}

void testToolbarTickWrap()
{
  FullScreenToolbarLogic logic(2000);
  unsigned int start = 0xFFFFFC18u; // 1000 ms before the 32-bit tick counter wraps
  logic.show(start);
  check(!logic.onTimer(false, start + 1999u), "wrap: visible before the delay");
  check(logic.onTimer(false, start + 2000u), "wrap: hidden after the delay across the wrap");
}

} // namespace

int main()
{
  testHostCompare();
  testAddRemove();
  testLimit();
  testHostList();
  testToolbarGeometry();
  testToolbarAutoHide();
  testToolbarPin();
  testToolbarTickWrap();
  printf("viewer-favorites-test: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
