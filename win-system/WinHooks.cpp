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
// Modified for FarSightVNC on 2026-11-14: key owners per extended key (Enter/Numpad Enter); stricter LocalKeys parsing.
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
  // Print Screen (also Alt+Print Screen) is for the local screenshot tools.
  if ((flags & LLKHF_INJECTED) || vkCode == VK_CAPITAL ||
      vkCode == VK_NUMLOCK || vkCode == VK_SCROLL || vkCode == VK_SNAPSHOT) {
    return HOOK_KEY_PASS;
  }
  // Post every physical key to the same queue, then swallow the source event.
  return HOOK_KEY_POST;
}

unsigned WinHooks::localKeyIndex(DWORD vkCode, bool extended)
{
  return (vkCode & 0xff) | (extended ? 0x100 : 0);
}

WinHooks::LocalKeySet WinHooks::parseLocalKeys(const TCHAR *text)
{
  LocalKeySet keys;
  if (text == 0) {
    return keys;
  }
  const TCHAR *p = text;
  while (*p != 0) {
    // One entry: up to the next comma, spaces around it ignored.
    const TCHAR *end = p;
    while (*end != 0 && *end != _T(',')) end++;
    const TCHAR *b = p;
    const TCHAR *e = end;
    while (b < e && (*b == _T(' ') || *b == _T('\t'))) b++;
    while (e > b && (e[-1] == _T(' ') || e[-1] == _T('\t'))) e--;
    // Two hex digits and the suffix: "BE" is a key, "BEe" its extended key.
    // At least the two digits come before the suffix: a lone "e" is not a
    // key.
    bool extended = e - b == 3 && (e[-1] == _T('e') || e[-1] == _T('E'));
    if (extended) e--;
    unsigned vk = 0;
    bool valid = e - b == 2;
    for (const TCHAR *c = b; valid && c < e; c++) {
      unsigned digit;
      if (*c >= _T('0') && *c <= _T('9')) digit = *c - _T('0');
      else if (*c >= _T('a') && *c <= _T('f')) digit = *c - _T('a') + 10;
      else if (*c >= _T('A') && *c <= _T('F')) digit = *c - _T('A') + 10;
      else { valid = false; break; }
      vk = vk * 16 + digit;
    }
    if (valid && vk >= 0x01 && vk <= 0xFE) {
      keys.set(localKeyIndex(vk, extended));
    }
    p = *end != 0 ? end + 1 : end;
  }
  return keys;
}

StringStorage WinHooks::formatLocalKeys(const LocalKeySet &keys)
{
  StringStorage text;
  for (unsigned i = 0; i < keys.size(); i++) {
    if (!keys.test(i)) continue;
    StringStorage entry;
    entry.format(_T("%s%02X%s"), text.isEmpty() ? _T("") : _T(","), i & 0xff,
                 (i & 0x100) ? _T("e") : _T(""));
    text.appendString(entry.getString());
  }
  return text;
}

void WinHooks::setLocalKeys(const LocalKeySet &keys)
{
  s_keyRouter.setLocalKeys(keys);
}

bool WinHooks::isLocalKey(DWORD vkCode, bool extended)
{
  return s_keyRouter.isLocalKey(vkCode, extended);
}

WinHooks::KeyRouter::KeyRouter()
{
  for (size_t i = 0; i < sizeof(m_localKeys) / sizeof(m_localKeys[0]); i++) {
    m_localKeys[i] = 0;
  }
  reset();
}

void WinHooks::KeyRouter::setLocalKeys(const LocalKeySet &keys)
{
  for (size_t w = 0; w < sizeof(m_localKeys) / sizeof(m_localKeys[0]); w++) {
    unsigned long long word = 0;
    for (unsigned b = 0; b < 64; b++) {
      if (keys.test(w * 64 + b)) word |= 1ull << b;
    }
    m_localKeys[w] = word;
  }
}

bool WinHooks::KeyRouter::isLocalKey(DWORD vkCode, bool extended) const
{
  unsigned i = localKeyIndex(vkCode, extended);
  return ((m_localKeys[i / 64].load() >> (i % 64)) & 1) != 0;
}

void WinHooks::KeyRouter::reset()
{
  memset(m_owner, OWNER_NONE, sizeof(m_owner));
}

WinHooks::HookKeyAction WinHooks::KeyRouter::route(DWORD vkCode, DWORD flags,
                                                   bool viewerOwnsKeyboard,
                                                   bool systemHasKeyDown)
{
  if (decideKeyAction(vkCode, flags) == HOOK_KEY_PASS) {
    return HOOK_KEY_PASS;
  }
  bool extended = (flags & LLKHF_EXTENDED) != 0;
  unsigned char &owner = m_owner[localKeyIndex(vkCode, extended)];
  if (flags & LLKHF_UP) {
    // A key-up without a recorded key-down was pressed before the hook was
    // installed, so Windows has its key-down. Windows also gets the key-up of
    // a key it has down (a key-down that bypassed the hook): the viewer, when
    // it has the focus, receives that key-up as an ordinary message.
    bool viewer = owner == OWNER_VIEWER && !systemHasKeyDown;
    owner = OWNER_NONE;
    return viewer ? HOOK_KEY_POST : HOOK_KEY_PASS;
  }
  // Autorepeat keeps the owner of the first key-down. A key that Windows
  // already has down (held when the hook was installed, or passed through
  // while the hook timed out) stays with Windows until its key-up.
  // A local key goes to Windows (screenshot tools...) as the bare key: the
  // modifiers the viewer owns were swallowed.
  if (owner == OWNER_NONE) {
    bool local = isLocalKey(vkCode, extended);
    owner = viewerOwnsKeyboard && !systemHasKeyDown && !local ? OWNER_VIEWER : OWNER_SYSTEM;
  }
  return owner == OWNER_VIEWER ? HOOK_KEY_POST : HOOK_KEY_PASS;
}

WinHooks::HookKeyAction WinHooks::routeKey(DWORD vkCode, DWORD flags, bool viewerOwnsKeyboard,
                                           bool systemHasKeyDown)
{
  return s_keyRouter.route(vkCode, flags, viewerOwnsKeyboard, systemHasKeyDown);
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
