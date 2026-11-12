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
//

#include "util/CommonHeader.h"
#include "win-system/HookEventListener.h"

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
    HookKeyAction route(DWORD vkCode, DWORD flags, bool viewerOwnsKeyboard);
  private:
    enum Owner { OWNER_NONE, OWNER_VIEWER, OWNER_SYSTEM };
    unsigned char m_owner[256];
  };

  // Routes a key event of the installed hook (see KeyRouter). The routing
  // state starts empty at every registration.
  static HookKeyAction routeKey(DWORD vkCode, DWORD flags, bool viewerOwnsKeyboard);

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
