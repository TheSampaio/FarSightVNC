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

// Deterministic test for the tab shortcuts of a shell window
// (tvnviewer/shell/ShellShortcuts.h): which key combinations open, close and
// switch tabs, and which ones are left alone.

#include "tvnviewer/shell/ShellShortcuts.h"

using fsv::shell::TabShortcut;
using fsv::shell::tabShortcutFromKey;

// Called by main() of FavoritesTest.cpp with its check().
void testShellShortcuts(void (*check)(bool, const char *))
{
  using namespace fsv::shell;
  check(tabShortcutFromKey(kKeyT, true, false, false) == TabShortcut::NewTab, "Ctrl+T opens a tab");
  check(tabShortcutFromKey(kKeyW, true, false, false) == TabShortcut::CloseTab, "Ctrl+W closes the tab");
  check(tabShortcutFromKey(kKeyTab, true, false, false) == TabShortcut::NextTab, "Ctrl+Tab goes to the next tab");
  check(tabShortcutFromKey(kKeyTab, true, true, false) == TabShortcut::PreviousTab,
        "Ctrl+Shift+Tab goes to the previous tab");
  check(tabShortcutFromKey(kKeyT, false, false, false) == TabShortcut::None, "T alone is not a shortcut");
  check(tabShortcutFromKey(kKeyW, false, false, false) == TabShortcut::None, "W alone is not a shortcut");
  check(tabShortcutFromKey(kKeyTab, false, false, false) == TabShortcut::None, "Tab alone is not a shortcut");
  check(tabShortcutFromKey(kKeyTab, false, true, false) == TabShortcut::None, "Shift+Tab is not a shortcut");
  check(tabShortcutFromKey(kKeyT, true, true, false) == TabShortcut::None, "Ctrl+Shift+T is left alone");
  check(tabShortcutFromKey(kKeyW, true, true, false) == TabShortcut::None, "Ctrl+Shift+W is left alone");
  check(tabShortcutFromKey(kKeyT, true, false, true) == TabShortcut::None, "Ctrl+Alt+T is left alone");
  check(tabShortcutFromKey(kKeyW, true, false, true) == TabShortcut::None, "Ctrl+Alt+W is left alone");
  check(tabShortcutFromKey(kKeyTab, true, false, true) == TabShortcut::None, "Ctrl+Alt+Tab is left alone");
  check(tabShortcutFromKey(0x4E, true, false, false) == TabShortcut::None, "Ctrl+N is not a tab shortcut");
  check(tabShortcutFromKey(0x4F, true, false, false) == TabShortcut::None, "Ctrl+O is not a tab shortcut");
}
