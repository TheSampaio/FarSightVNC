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

#ifndef _SHELL_SHORTCUTS_H_
#define _SHELL_SHORTCUTS_H_

namespace fsv::shell {

// Keyboard shortcuts of the tabs of a shell window. They only apply while the
// Home page or the Settings page has the keyboard: in a session every key
// belongs to the remote computer. No Win32 here, so that it can be tested.
enum class TabShortcut
{
  None,
  NewTab,
  CloseTab,
  NextTab,
  PreviousTab
};

// Virtual key codes of the keys that matter (winuser.h).
constexpr unsigned kKeyTab = 0x09;
constexpr unsigned kKeyT = 0x54;
constexpr unsigned kKeyW = 0x57;

// Ctrl+T new tab, Ctrl+W close the active tab, Ctrl+Tab next tab and
// Ctrl+Shift+Tab previous tab; any other combination is none. Alt never
// belongs to them (Ctrl+Alt+T is AltGr+T on some keyboards).
inline TabShortcut tabShortcutFromKey(unsigned key, bool ctrl, bool shift, bool alt)
{
  if (!ctrl || alt) {
    return TabShortcut::None;
  }
  switch (key) {
  case kKeyT:
    return shift ? TabShortcut::None : TabShortcut::NewTab;
  case kKeyW:
    return shift ? TabShortcut::None : TabShortcut::CloseTab;
  case kKeyTab:
    return shift ? TabShortcut::PreviousTab : TabShortcut::NextTab;
  default:
    return TabShortcut::None;
  }
}

}

#endif
