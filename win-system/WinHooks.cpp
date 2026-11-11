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
// Modified for FarSightVNC on 2026-11-11: a key-up takes the same path (posted or passed to Windows) as its key-down.
//

#include "WinHooks.h"
#include "win-system/SystemException.h"

#include <string.h>

HHOOK WinHooks::s_hooks = 0;
HookEventListener *WinHooks::s_eventListener = 0;
WinHooks::KeyRouter WinHooks::s_keyRouter;

WinHooks::WinHooks()
{

}

WinHooks::~WinHooks()
{
  // The owner (ViewerWindow) calls unregisterKeyboardHook(this) itself. Removing
  // the process wide hook here would break another window that owns it.
}

LRESULT CALLBACK WinHooks::lowLevelKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam)
{
  if (nCode < 0) {
    return CallNextHookEx(s_hooks, nCode, wParam, lParam);
  }
  if (s_eventListener != 0 && s_eventListener->onHookProc(nCode, wParam, lParam)) {
    // Processing is successful. Don't pass it to the next hook procedure.
    return true;
  } else {
    // Calling the CallNextHookEx function to chain to the next hook procedure.
    return CallNextHookEx(s_hooks, nCode, wParam, lParam);
  }
}

void WinHooks::registerKeyboardHook(HookEventListener *hookEventListener)
{
  unregisterKeyboardHook(0);
  // Keys held now were pressed without the hook: Windows owns their release.
  s_keyRouter.reset();
  s_eventListener = hookEventListener;
  HINSTANCE hinst = GetModuleHandle(0);
  s_hooks = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardHook, hinst, 0);
  if (s_hooks == NULL) {
    throw SystemException(_T("Unnable to set hooks"));
  }
}

void WinHooks::unregisterKeyboardHook(HookEventListener *hookEventListener)
{
  if (hookEventListener != 0 && hookEventListener != s_eventListener) {
    return;
  }
  if (s_hooks != 0) {
    UnhookWindowsHookEx(s_hooks);
  }
  s_hooks = 0;
  s_eventListener = 0;
}

bool WinHooks::isRegisteredFor(HookEventListener *hookEventListener)
{
  return s_hooks != 0 && s_eventListener == hookEventListener;
}

unsigned char WinHooks::applyKeyToStateTable(BYTE *keys, WPARAM virtKey, LPARAM lParam)
{
  bool isDown = (lParam & 0x80000000) == 0;
  bool extended = (lParam & 0x1000000) != 0;
  unsigned char key = (unsigned char)virtKey;
  if (key == VK_SHIFT) key = ((lParam >> 16) & 0xff) == 0x36 ? VK_RSHIFT : VK_LSHIFT;
  if (key == VK_CONTROL) key = extended ? VK_RCONTROL : VK_LCONTROL;
  if (key == VK_MENU) key = extended ? VK_RMENU : VK_LMENU;
  keys[key] = (BYTE)((keys[key] & 1) | (isDown ? 0x80 : 0));
  keys[VK_SHIFT] = keys[VK_LSHIFT] | keys[VK_RSHIFT];
  keys[VK_CONTROL] = keys[VK_LCONTROL] | keys[VK_RCONTROL];
  keys[VK_MENU] = keys[VK_LMENU] | keys[VK_RMENU];
  return key;
}

WinHooks::HookKeyAction WinHooks::decideKeyAction(DWORD vkCode, DWORD flags)
{
  if ((flags & LLKHF_INJECTED) || vkCode == VK_CAPITAL ||
      vkCode == VK_NUMLOCK || vkCode == VK_SCROLL) {
    return HOOK_KEY_PASS;
  }
  // Post every physical key to the same queue, then swallow the source event.
  return HOOK_KEY_POST;
}

WinHooks::KeyRouter::KeyRouter()
{
  reset();
}

void WinHooks::KeyRouter::reset()
{
  memset(m_owner, OWNER_NONE, sizeof(m_owner));
}

WinHooks::HookKeyAction WinHooks::KeyRouter::route(DWORD vkCode, DWORD flags,
                                                   bool viewerOwnsKeyboard)
{
  if (decideKeyAction(vkCode, flags) == HOOK_KEY_PASS) {
    return HOOK_KEY_PASS;
  }
  unsigned char &owner = m_owner[vkCode & 0xff];
  if (flags & LLKHF_UP) {
    // A key-up without a recorded key-down was pressed before the hook was
    // installed, so Windows has its key-down.
    bool viewer = owner == OWNER_VIEWER;
    owner = OWNER_NONE;
    return viewer ? HOOK_KEY_POST : HOOK_KEY_PASS;
  }
  // Autorepeat keeps the owner of the first key-down.
  if (owner == OWNER_NONE) {
    owner = viewerOwnsKeyboard ? OWNER_VIEWER : OWNER_SYSTEM;
  }
  return owner == OWNER_VIEWER ? HOOK_KEY_POST : HOOK_KEY_PASS;
}

WinHooks::HookKeyAction WinHooks::routeKey(DWORD vkCode, DWORD flags, bool viewerOwnsKeyboard)
{
  return s_keyRouter.route(vkCode, flags, viewerOwnsKeyboard);
}

LPARAM WinHooks::makeKeyMessageLParam(DWORD scanCode, DWORD flags)
{
  // Set the repeat count for the current message bits.
  LPARAM newLParam = 1;
  // Set the scan code bits.
  newLParam |= (scanCode & 0xff) << 16;
  // Set the extended key bit.
  newLParam |= (flags & LLKHF_EXTENDED) << 24;
  // Set the context code bit.
  newLParam |= ((flags & LLKHF_ALTDOWN) > 0) << 29;
  // Set the transition state bit.
  newLParam |= ((flags & LLKHF_UP) > 0) << 31;
  if (flags & LLKHF_UP) newLParam |= 0x40000000;
  return newLParam;
}
