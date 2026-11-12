// Copyright (C) 2010,2011,2012 GlavSoft LLC.
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
// Modified for FarSightVNC on 2026-11-01: symmetric key press/release, scancode aware translation, Left/Right Shift.
// Modified for FarSightVNC on 2026-11-03: one Shift_L for both Shift keys; keypad digits when Shift+digit is a dead key.
// Modified for FarSightVNC on 2026-11-04: release a Shift whose key-up Windows did not send; fake (E0) Shifts.; keyboard trace in the debug log
// Modified for FarSightVNC on 2026-11-07: Shift released around characters a TightVNC server types with AltGr (ABNT2 Shift+'/?').
// Modified for FarSightVNC on 2026-11-11: Print Screen is never sent to the server.
//

#include "RfbKeySym.h"
#include "util/CommonHeader.h"

#define XK_MISCELLANY
#include "rfb/keysymdef.h"

RfbKeySym::RfbKeySym(RfbKeySymListener *extKeySymListener, LogWriter *log)
: m_extKeySymListener(extKeySymListener),
  m_allowProcessCharEvent(false),
  m_allowProcessDoubleChar(false),
  m_doubleDeadCatched(false),
  m_leftMetaIsPressed(false),
  m_rightMetaIsPressed(false),
  m_log(log),
  m_winKeyIgnore(true),
  m_layoutOverride(0),
  m_toggleKeysOverridden(false),
  m_capsLockOverride(false),
  m_scrollLockOverride(false),
  m_keyStateOverride(0),
  m_shiftKeysDown(0)
{
  clearKeyState();
}

RfbKeySym::~RfbKeySym()
{
}

void RfbKeySym::sendModifier(unsigned char virtKey, bool down)
{
  virtKey = distinguishLeftRightModifier(virtKey, false);
  UINT scan = MapVirtualKeyEx(virtKey, MAPVK_VK_TO_VSC, getKeyboardLayout()) & 0xff;
  unsigned data = scan << 16;
  if (virtKey == VK_RCONTROL || virtKey == VK_RMENU) data |= 0x1000000;
  if (!down) data |= 0x80000000;
  processKeyEvent(virtKey, data);
}

void RfbKeySym::processKeyEvent(unsigned short virtKey,
                                unsigned int addKeyData)
{
  m_log->debug(_T("Keyboard VK=%04x data=%08x LShift=%d RShift=%d"), virtKey, addKeyData, (int)isKeyHeld(VK_LSHIFT), (int)isKeyHeld(VK_RSHIFT));
  // Print Screen stays local (screenshots, Snipping Tool), also with Alt.
  if (virtKey == VK_SNAPSHOT) return;
  bool down = (addKeyData & 0x80000000) == 0;
  unsigned int scanCode = (addKeyData >> 16) & 0xff;
  bool extended = (addKeyData & 0x1000000) != 0; // 24 bit
  bool shiftKey = virtKey == VK_SHIFT || virtKey == VK_LSHIFT || virtKey == VK_RSHIFT;
  // A Shift key has no E0 prefix: the keyboard sends E0 2A / E0 AA ("fake
  // Shifts") around some keys. They are the same physical Shift key.
  if (shiftKey) extended = false;
  unsigned int physicalKey = scanCode ? (scanCode | (extended ? 0x100 : 0)) :
                                        (0x200 | virtKey);
  std::map<unsigned int, std::vector<UINT32> >::iterator old = m_pressedKeys.find(physicalKey);
  // The Win key is ignored while the keyboard hook is off, but a Win key that
  // is already held on the server must still be released.
  if (m_winKeyIgnore && (virtKey == VK_LWIN || virtKey == VK_RWIN) &&
      (down || old == m_pressedKeys.end())) return;
  if (!down) {
    if (old == m_pressedKeys.end()) return;
    UINT32 ignoredSym;
    bool ctrlAlt = (isPressed(VK_LCONTROL) || isPressed(VK_RCONTROL)) &&
                   (isPressed(VK_LMENU) || isPressed(VK_RMENU));
    bool releaseCtrlAlt = ctrlAlt && !old->second.empty() &&
                          !m_keyMap.virtualCodeToKeySym(&ignoredSym, virtKey & 255);
    if (releaseCtrlAlt) releaseModifiers();
    for (size_t i = 0; i < old->second.size(); i++)
      sendKeySymEvent(old->second[i], false);
    if (releaseCtrlAlt) restoreModifiers();
    m_pressedKeys.erase(old);
    if (shiftKey) releaseShiftsNotHeld();
  } else if (old != m_pressedKeys.end()) {
    // Repeat the cached printable/special key without translating it again.
    // Modifiers are state transitions and must not repeat.
    bool modifier = virtKey == VK_SHIFT || virtKey == VK_CONTROL || virtKey == VK_MENU ||
      (virtKey >= VK_LSHIFT && virtKey <= VK_RMENU) || virtKey == VK_LWIN || virtKey == VK_RWIN;
    if (!modifier) sendCharKeySyms(old->second);
    return;
  }

  unsigned char key = distinguishLeftRightModifier((unsigned char)virtKey,
                            virtKey == VK_SHIFT ? scanCode == 0x36 : extended);
  m_viewerKeyState[key] = down ? 128 : 0;
  m_viewerKeyState[VK_SHIFT] = m_viewerKeyState[VK_LSHIFT] | m_viewerKeyState[VK_RSHIFT];
  m_viewerKeyState[VK_CONTROL] = m_viewerKeyState[VK_LCONTROL] | m_viewerKeyState[VK_RCONTROL];
  m_viewerKeyState[VK_MENU] = m_viewerKeyState[VK_LMENU] | m_viewerKeyState[VK_RMENU];
  m_viewerKeyState[VK_CAPITAL] = isToggled(VK_CAPITAL) ? 1 : 0;
  m_serverKeyState[key] = down ? 128 : 0;
  if (!down) return;

  std::vector<UINT32> symbols;
  UINT32 rfbSym;
  if (m_keyMap.virtualCodeToKeySym(&rfbSym, key)) {
    if (rfbSym == XK_Return && extended) {
      rfbSym = XK_KP_Enter;
    }
    symbols.push_back(rfbSym);
  } else {
    std::wstring chars;
    bool needReleaseModifiers = vkCodeToString(key, scanCode | (extended ? 0xe000 : 0), &chars);
    bool ctrlAlt = (isPressed(VK_LCONTROL) || isPressed(VK_RCONTROL)) &&
                   (isPressed(VK_LMENU) || isPressed(VK_RMENU));
    if (ctrlAlt && needReleaseModifiers) releaseModifiers();
    for (size_t i = 0; i < chars.size(); i++) {
      if (m_keyMap.unicodeCharToKeySym(chars[i], &rfbSym)) {
        symbols.push_back(rfbSym);
      } else {
        m_log->error(_T("Can't translate the %#4.4x unicode character to an")
          _T(" rfb symbol to send it"), (unsigned int)chars[i]);
      }
    }
    // A TightVNC server types a character after probing its key with and
    // without Shift by ToUnicodeEx(), which leaves a dead Shift variant
    // pending: on ABNT2 and US-International "6" arrives as "¨6" / "^6".
    // The server injects keypad keysyms directly, and a keypad digit typed
    // through its virtual key gives the digit whatever the Num Lock state.
    if (symbols.size() == 1 && symbols[0] >= '0' && symbols[0] <= '9' &&
        !isPressed(VK_LSHIFT) && !isPressed(VK_RSHIFT) &&
        !isPressed(VK_LCONTROL) && !isPressed(VK_RCONTROL) &&
        !isPressed(VK_LMENU) && !isPressed(VK_RMENU) &&
        shiftVariantIsDeadKey(key, scanCode | (extended ? 0xe000 : 0))) {
      symbols[0] = XK_KP_0 + (symbols[0] - '0');
    }
    sendCharKeySyms(symbols);
    if (ctrlAlt && needReleaseModifiers) restoreModifiers();
    m_pressedKeys[physicalKey] = symbols;
    return;
  }
  for (size_t i = 0; i < symbols.size(); i++) sendKeySymEvent(symbols[i], true);
  m_pressedKeys[physicalKey] = symbols;
}

bool RfbKeySym::needsShiftReleased(const std::vector<UINT32> &symbols)
{
  if (m_shiftKeysDown == 0 || isPressed(VK_LCONTROL) || isPressed(VK_RCONTROL) ||
      isPressed(VK_LMENU) || isPressed(VK_RMENU)) {
    return false;
  }
  HKL layout = getKeyboardLayout();
  for (size_t i = 0; i < symbols.size(); i++) {
    WCHAR ch;
    if (!m_keyMap.keySymToUnicodeChar(symbols[i], &ch)) continue;
    SHORT scan = VkKeyScanExW(ch, layout);
    // Bit 1 of the high byte is Ctrl, bit 2 is Alt.
    if (scan != -1 && (scan & 0x600) != 0) return true;
  }
  return false;
}

void RfbKeySym::sendCharKeySyms(const std::vector<UINT32> &symbols)
{
  // A TightVNC server types a character by VkKeyScanEx() and presses the
  // modifiers it needs: ABNT2 '?' becomes AltGr+W. With Shift held it pressed
  // Ctrl and Alt and then toggled Shift, giving Ctrl+Shift / Alt+Shift
  // (input language hotkeys) and a Ctrl+Alt+Shift chord on the remote. Shift
  // is released around such a character; m_shiftKeysDown is unchanged, so the
  // physical Shift key-up still sends exactly one Shift_L up.
  bool releaseShift = needsShiftReleased(symbols);
  if (releaseShift) sendVerbatimKeySymEvent(XK_Shift_L, false);
  for (size_t i = 0; i < symbols.size(); i++) sendKeySymEvent(symbols[i], true);
  if (releaseShift) sendVerbatimKeySymEvent(XK_Shift_L, true);
}

void RfbKeySym::releaseShiftsNotHeld()
{
  // With both Shift keys held Windows sends no key-up for the Shift released
  // first, so the server kept Shift down (and the viewer typed shifted
  // characters) until the focus was lost.
  std::map<unsigned int, std::vector<UINT32> >::iterator it = m_pressedKeys.begin();
  while (it != m_pressedKeys.end()) {
    unsigned char vk = 0;
    if (it->second.size() == 1 && it->second[0] == XK_Shift_L) vk = VK_LSHIFT;
    if (it->second.size() == 1 && it->second[0] == XK_Shift_R) vk = VK_RSHIFT;
    if (vk == 0 || isKeyHeld(vk)) {
      ++it;
      continue;
    }
    sendKeySymEvent(it->second[0], false);
    m_viewerKeyState[vk] = 0;
    m_serverKeyState[vk] = 0;
    m_viewerKeyState[VK_SHIFT] = m_viewerKeyState[VK_LSHIFT] | m_viewerKeyState[VK_RSHIFT];
    m_pressedKeys.erase(it++);
  }
}

bool RfbKeySym::isKeyHeld(unsigned char virtKey)
{
  if (m_keyStateOverride != 0) return (m_keyStateOverride[virtKey] & 0x80) != 0;
  return (GetKeyState(virtKey) & 0x8000) != 0;
}

bool RfbKeySym::shiftVariantIsDeadKey(unsigned short virtKey, unsigned int scanCode)
{
  unsigned char shiftState[256] = {0};
  shiftState[VK_SHIFT] = shiftState[VK_LSHIFT] = 128;
  WCHAR outBuff[20];
  // Bit 2 probes without changing the layout's dead-key buffer.
  return ToUnicodeEx(virtKey, scanCode, shiftState, outBuff,
                     sizeof(outBuff) / sizeof(WCHAR), 0x4, getKeyboardLayout()) == -1;
}

WCHAR RfbKeySym::GettingCharFromCtrlSymbol(WCHAR ch)
{
  bool ctrlPressed = isPressed(VK_LCONTROL) || isPressed(VK_RCONTROL);
  bool altPressed = isPressed(VK_LMENU) || isPressed(VK_RMENU);
  bool shiftPressed = isPressed(VK_LSHIFT) || isPressed(VK_RSHIFT);
  unsigned int oldCh = (unsigned int)ch;
  if (ctrlPressed && !altPressed && ch < 32) {
    if (ch >= 1 && ch <= 26 && !shiftPressed) {
      ch += 96;
    }
    else {
      ch += 64;
    }
    m_log->debug(_T("The %u char is a control symbol then")
      _T(" it will be increased to %u"),
      oldCh, (unsigned int)ch);
  }
  return ch;
}

bool RfbKeySym::TryTranslateNotPrintableToUnicode(unsigned short virtKey, unsigned int scanCode, HKL currentLayout, WCHAR *unicodeChar)
{
  WCHAR outBuff[20];
  m_log->debug(_T("Was get a not printable symbol then try get a printable")
    _T(" with turned off the ctrl and alt modifiers"));
  // E.g if pressed Ctrl + Alt + A
  // Try found char without modificators
  unsigned char withoutCtrlAltKbdState[256];
  memcpy(withoutCtrlAltKbdState, m_viewerKeyState, sizeof(withoutCtrlAltKbdState));
  withoutCtrlAltKbdState[VK_LCONTROL] = 0;
  withoutCtrlAltKbdState[VK_RCONTROL] = 0;
  withoutCtrlAltKbdState[VK_CONTROL] = 0;
  withoutCtrlAltKbdState[VK_LMENU] = 0;
  withoutCtrlAltKbdState[VK_RMENU] = 0;
  withoutCtrlAltKbdState[VK_MENU] = 0;
  int count = ToUnicodeEx(virtKey, scanCode, withoutCtrlAltKbdState, outBuff,
    sizeof(outBuff) / sizeof(WCHAR),
    0x4, currentLayout);
  if (count > 0) {
    outBuff[count] = 0;
  }
  else {
    outBuff[0] = 0;
  }
  m_log->debug(_T("ToUnicodeEx() without ctrl and alt return %d wchars : %S"), count, outBuff);

  if (count == 1) { // other case will be ignored
    *unicodeChar = outBuff[0];
    return true;
  }
  return false;
}

bool RfbKeySym::vkCodeToString(unsigned short virtKey, unsigned int scanCode, std::wstring *res)
{
  WCHAR outBuff[20];
  HKL currentLayout = getKeyboardLayout();
  // Bit 2 probes without changing the layout's dead-key buffer.
  int count = ToUnicodeEx(virtKey, scanCode, m_viewerKeyState, outBuff,
                          sizeof(outBuff) / sizeof(WCHAR), 0x4, currentLayout);
  if (count == -1) {
    // Some layouts (US International apostrophe) return a dead-key result
    // for the second accent, yet TranslateMessage emits two WM_CHARs.
    m_allowProcessDoubleChar = m_allowProcessCharEvent;
    m_allowProcessCharEvent = true;
    return false;
  }
  if (m_allowProcessCharEvent) {
    if (count > 1)
      m_allowProcessDoubleChar = true;
    return false;
  }
  if (count > 0) {
    for (int i = 0; i < count; i++)
      res->push_back(GettingCharFromCtrlSymbol(outBuff[i]));
    return true;
  }
  if (count == 0) {
    WCHAR unicodeChar;
    if (TryTranslateNotPrintableToUnicode(virtKey, scanCode, currentLayout, &unicodeChar)) {
      res->push_back(unicodeChar);
    }
  }
  return false;
}

void RfbKeySym::processCharEvent(WCHAR charCode,
                                 unsigned int addKeyData)
{
  if (m_allowProcessCharEvent) {
    m_log->debug(_T("processCharEvent() function called with alowed processing:")
               _T(" charCode = %#4.4x, addKeyData = %#x"),
               (unsigned int)charCode, addKeyData);
    // For keyboards with dead keys
    if (m_allowProcessDoubleChar) {
      m_allowProcessDoubleChar = false;
    } else {
      m_allowProcessCharEvent = false;
    }

    UINT32 rfbSym;
    if (m_keyMap.unicodeCharToKeySym(charCode, &rfbSym)) {
      bool ctrlAlt = (isPressed(VK_LCONTROL) || isPressed(VK_RCONTROL)) &&
                     (isPressed(VK_LMENU) || isPressed(VK_RMENU));
      if (ctrlAlt) releaseModifiers();
      sendKeySymEvent(rfbSym, true);
      sendKeySymEvent(rfbSym, false);
      if (ctrlAlt) restoreModifiers();
    } else {
      m_log->error(_T("Can't translate the %#4.4x unicode character to an")
                 _T(" rfb symbol to send it"), (unsigned int)charCode);
    }
  }
}

void RfbKeySym::processFocusRestoration()
{
  m_log->info(_T("Process focus restoration in the RfbKeySym class"));

  // Send a modifier key state based on physical keyboard state, not thread key message queue.
  unsigned char keys[] = {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
                          VK_LSHIFT, VK_RSHIFT};
  for (int i = 0; i < sizeof(keys); i++) {
    unsigned char key = keys[i];
    if ((GetAsyncKeyState(key) & 0x8000) && !isPressed(key)) {
      sendModifier(key, true);
    }
  }
}

void RfbKeySym::processFocusLoss()
{
  for (std::map<unsigned int, std::vector<UINT32> >::iterator it = m_pressedKeys.begin();
       it != m_pressedKeys.end(); ++it) {
    for (size_t i = 0; i < it->second.size(); i++)
      sendKeySymEvent(it->second[i], false);
  }
  m_pressedKeys.clear();
  m_shiftKeysDown = 0;
  m_leftMetaIsPressed = false;
  m_rightMetaIsPressed = false;
  clearKeyState();
  m_allowProcessCharEvent = false;
  m_allowProcessDoubleChar = false;
  m_doubleDeadCatched = false;
}

void RfbKeySym::sendCtrlAltDel()
{
  releaseModifier(VK_RWIN);
  releaseModifier(VK_LWIN);
  releaseModifier(VK_LSHIFT);
  releaseModifier(VK_RSHIFT);
  releaseMeta();

  sendVerbatimKeySymEvent(XK_Control_L, true);
  sendVerbatimKeySymEvent(XK_Alt_L, true);
  sendVerbatimKeySymEvent(XK_Delete, true);
  sendVerbatimKeySymEvent(XK_Delete, false);
  sendVerbatimKeySymEvent(XK_Alt_L, false);
  sendVerbatimKeySymEvent(XK_Control_L, false);

  restoreMeta();
  restoreModifier(VK_RSHIFT);
  restoreModifier(VK_LSHIFT);
  restoreModifier(VK_LWIN);
  restoreModifier(VK_RWIN);
}

void RfbKeySym::setWinKeyIgnore(bool winKeyIgnore)
{
  m_winKeyIgnore = winKeyIgnore;
}

void RfbKeySym::releaseModifiers()
{
  releaseModifier(VK_LCONTROL);
  releaseModifier(VK_RCONTROL);
  releaseModifier(VK_LMENU);
  releaseModifier(VK_RMENU);
}

void RfbKeySym::restoreModifiers()
{
  restoreModifier(VK_LCONTROL);
  restoreModifier(VK_RCONTROL);
  restoreModifier(VK_LMENU);
  restoreModifier(VK_RMENU);
}

void RfbKeySym::releaseModifier(unsigned char modifier)
{
  UINT32 rfbSym;
  if (isPressed(modifier)) {
    bool success = m_keyMap.virtualCodeToKeySym(&rfbSym, modifier);
    _ASSERT(success);
    sendKeySymEvent(rfbSym, false);
  }
}

void RfbKeySym::releaseMeta()
{
  if (m_leftMetaIsPressed) {
    sendKeySymEvent(XK_Meta_L, false);
  }
  if (m_rightMetaIsPressed) {
    sendKeySymEvent(XK_Meta_R, false);
  }
}

void RfbKeySym::restoreMeta()
{
  if (m_leftMetaIsPressed) {
    sendKeySymEvent(XK_Meta_L, true);
  }
  if (m_rightMetaIsPressed) {
    sendKeySymEvent(XK_Meta_R, true);
  }
}

void RfbKeySym::restoreModifier(unsigned char modifier)
{
  UINT32 rfbSym;
  if (isPressed(modifier)) {
    bool success = m_keyMap.virtualCodeToKeySym(&rfbSym, modifier);
    _ASSERT(success);
    sendKeySymEvent(rfbSym, true);
  }
}

void RfbKeySym::checkAndSendDiff(unsigned char virtKey, unsigned char state)
{
  bool testedState = (state & 128) != 0;
  m_viewerKeyState[virtKey] = testedState ? 128 : 0;
  virtKey = distinguishLeftRightModifier(virtKey, false);

  bool srvState = (m_serverKeyState[virtKey] & 128) != 0;
  m_serverKeyState[virtKey] = testedState ? 128 : 0;

  if (testedState != srvState) {
    UINT32 rfbSym;
    bool success = m_keyMap.virtualCodeToKeySym(&rfbSym, virtKey);
    _ASSERT(success);
    sendKeySymEvent(rfbSym, testedState);
  }
}

void RfbKeySym::clearKeyState()
{
  memset(m_viewerKeyState, 0, sizeof(m_viewerKeyState));
  memset(m_serverKeyState, 0, sizeof(m_serverKeyState));
}

bool RfbKeySym::isPressed(unsigned char virtKey)
{
  return (m_serverKeyState[virtKey] & 128) != 0;
}

void RfbKeySym::sendKeySymEvent(unsigned int rfbKeySym, bool down)
{
  // A TightVNC server releases and presses VK_SHIFT (the Left Shift scan
  // code) around characters that need other modifiers, e.g. ABNT2 '?' is
  // typed as AltGr+W. With Shift_R held this left the Left Shift down on the
  // server and Shift_R kept the character from being typed. Both Shift keys
  // are therefore one Shift_L for the server: pressed with the first key and
  // released with the last one.
  if (rfbKeySym == XK_Shift_L || rfbKeySym == XK_Shift_R) {
    unsigned char bit = rfbKeySym == XK_Shift_L ? 1 : 2;
    unsigned char before = m_shiftKeysDown;
    m_shiftKeysDown = down ? (before | bit) : (before & ~bit);
    if (down ? before != 0 : m_shiftKeysDown != 0) return;
    rfbKeySym = XK_Shift_L;
  }

  // Translate Alt to Meta if Scroll Lock is on.
  if (rfbKeySym == XK_Alt_L || rfbKeySym == XK_Alt_R) {
    bool scrollLockOn = down ? isToggled(VK_SCROLL) :
      (rfbKeySym == XK_Alt_L ? m_leftMetaIsPressed : m_rightMetaIsPressed);
    if (scrollLockOn) {
      if (rfbKeySym == XK_Alt_L) {
        rfbKeySym = XK_Meta_L;
        m_leftMetaIsPressed = down;
      } else { // assuming (rfbKeySym == XK_Alt_R)
        rfbKeySym = XK_Meta_R;
        m_rightMetaIsPressed = down;
      }
    }
  }

  // Send the keyboard event.
  sendVerbatimKeySymEvent(rfbKeySym, down);
}

void RfbKeySym::sendVerbatimKeySymEvent(unsigned int rfbKeySym, bool down)
{
  m_log->debug(_T("Send keysym %#x down=%d"), rfbKeySym, (int)down);
  m_extKeySymListener->onRfbKeySymEvent(rfbKeySym, down);
}

unsigned char RfbKeySym::distinguishLeftRightModifier(unsigned char virtKey,
                                                      bool isRightHint)
{
  if (virtKey == VK_CONTROL) {
    virtKey = isRightHint ? VK_RCONTROL : VK_LCONTROL;
  }
  if (virtKey == VK_MENU) {
    virtKey = isRightHint ? VK_RMENU : VK_LMENU;
  }
  if (virtKey == VK_SHIFT) {
    virtKey = isRightHint ? VK_RSHIFT : VK_LSHIFT;
  }
  return virtKey;
}

void RfbKeySym::setKeyboardLayout(HKL layout)
{
  m_layoutOverride = layout;
}

void RfbKeySym::setKeyStateOverride(const unsigned char *keys)
{
  m_keyStateOverride = keys;
}

void RfbKeySym::setToggleKeysOverride(bool capsLockOn, bool scrollLockOn)
{
  m_toggleKeysOverridden = true;
  m_capsLockOverride = capsLockOn;
  m_scrollLockOverride = scrollLockOn;
}

HKL RfbKeySym::getKeyboardLayout()
{
  return m_layoutOverride != 0 ? m_layoutOverride : GetKeyboardLayout(0);
}

bool RfbKeySym::isToggled(int virtKey)
{
  if (m_toggleKeysOverridden) {
    return virtKey == VK_CAPITAL ? m_capsLockOverride : m_scrollLockOverride;
  }
  return (GetKeyState(virtKey) & 1) != 0;
}
