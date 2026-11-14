// Copyright (C) 2013 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
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
//-------------------------------------------------------------------------
//
// Modified for FarSightVNC on 2026-11-01: pure helpers for the low level keyboard hook.
// Modified for FarSightVNC on 2026-11-11: a key-up takes the same path (posted or passed to Windows) as its key-down; Print Screen stays local.
// Modified for FarSightVNC on 2026-11-14: a key already held by Windows when the hook sees it first stays with Windows (no remote Ctrl stuck).
// Modified for FarSightVNC on 2026-11-14: user-configurable local keys (Settings, Input, Local shortcuts) stay with Windows.
//

#include "util/CommonHeader.h"
#include "win-system/HookEventListener.h"

#include <atomic>
#include <bitset>

// Class for intercepting keyboard on low level.
class WinHooks
{
public:
  WinHooks();
  ~WinHooks();

  // Hook procedure.
  static LRESULT CALLBACK lowLevelKeyboardHook(int nCode,
                                               WPARAM wParam,
                                               LPARAM lParam);

  // What the keyboard hook does with a key event.
  enum HookKeyAction {
    HOOK_KEY_PASS,
    HOOK_KEY_POST
  };

  // Pure decision function of the low level hook (unit-testable): keys that
  // always stay with Windows (injected, lock keys, Print Screen).
  static HookKeyAction decideKeyAction(DWORD vkCode, DWORD flags);

  // Local keys: keys the user keeps for Windows and local applications (a
  // screenshot tool on Numpad Minus...). They are never sent to the server.
  // Bit localKeyIndex(vk, extended): the virtual key, plus 0x100 for an
  // extended key (Numpad Enter is not Enter, Insert is not Numpad 0).
  typedef std::bitset<512> LocalKeySet;
  static unsigned localKeyIndex(DWORD vkCode, bool extended);
  // Stored form: comma separated hexadecimal virtual keys, "e" after the two
  // digits of an extended key ("6D,2De"). Invalid entries are ignored,
  // duplicates merged.
  static LocalKeySet parseLocalKeys(const TCHAR *text);
  static StringStorage formatLocalKeys(const LocalKeySet &keys);
  // Local keys of the hook and of the viewer windows (call from the window
  // thread that installs the hook).
  static void setLocalKeys(const LocalKeySet &keys);
  static bool isLocalKey(DWORD vkCode, bool extended);

  // Routes each physical key event so that a key-up takes the same path as
  // its key-down. A key pressed while the viewer owned the keyboard is posted
  // to the viewer until it is released, even if the focus moved meanwhile. A
  // key pressed before (held when the viewer took the keyboard) stays with
  // Windows until it is released. Otherwise Windows saw a key-down without
  // its key-up (Ctrl/Alt/Win stuck in the local desktop) and the viewer
  // pressed that key on the server again at every focus restoration.
  class KeyRouter
  {
  public:
    KeyRouter();
    void reset();
    // viewerOwnsKeyboard: the viewer is the foreground window, has the focus
    // and sends keys to the server (or is in full screen).
    // systemHasKeyDown: Windows already has this key down (GetAsyncKeyState
    // inside the hook, which reports the state before the current event). A
    // key-down of a key without an owner is then an autorepeat (or a late
    // event) of a key Windows saw pressed, e.g. held across an activation or
    // passed through while the hook timed out: Windows keeps it until its
    // key-up. Posting it to the viewer instead swallowed that key-up and left
    // the key down in Windows (and, at the next focus restoration, on the
    // server). For the same reason a key-up of a key Windows has down always
    // reaches Windows.
    // A local key is pressed for Windows, also with modifiers held. A
    // key-up still follows its key-down, so a key held while the local keys
    // change is released where it was pressed.
    HookKeyAction route(DWORD vkCode, DWORD flags, bool viewerOwnsKeyboard,
                        bool systemHasKeyDown);
    // Not cleared by reset().
    void setLocalKeys(const LocalKeySet &keys);
    bool isLocalKey(DWORD vkCode, bool extended) const;
  private:
    KeyRouter(const KeyRouter &);
    KeyRouter &operator=(const KeyRouter &);
    enum Owner { OWNER_NONE, OWNER_VIEWER, OWNER_SYSTEM };
    unsigned char m_owner[256];
    // LocalKeySet as words, read by the hook without a lock.
    std::atomic<unsigned long long> m_localKeys[512 / 64];
  };

  // Routes a key event of the installed hook (see KeyRouter). The routing
  // state starts empty at every registration.
  static HookKeyAction routeKey(DWORD vkCode, DWORD flags, bool viewerOwnsKeyboard,
                                bool systemHasKeyDown);

  // Builds the lParam of a WM_KEYDOWN/WM_KEYUP message from the
  // KBDLLHOOKSTRUCT scanCode and flags fields (unit-testable).
  static LPARAM makeKeyMessageLParam(DWORD scanCode, DWORD flags);

  // Registration of keyboard hook.
  static void registerKeyboardHook(HookEventListener *hookEventListener);

  // Unregistration of keyboard hook. The hook is process wide, so a listener
  // that does not own it leaves it untouched; 0 removes it unconditionally.
  static void unregisterKeyboardHook(HookEventListener *hookEventListener);

  // True when the hook is installed on behalf of this listener.
  static bool isRegisteredFor(HookEventListener *hookEventListener);

  // Applies a WM_KEYDOWN/WM_KEYUP style message to a GetKeyboardState table
  // (toggle bits are kept) and returns the left/right specific virtual key.
  // Keys posted by the hook never reach the thread's own key state.
  static unsigned char applyKeyToStateTable(BYTE *keys, WPARAM virtKey, LPARAM lParam);

private:
  // Hook object.
  static HHOOK s_hooks;

  // Handler to the processing hooks function.
  static HookEventListener *s_eventListener;

  static KeyRouter s_keyRouter;
};
