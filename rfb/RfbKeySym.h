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
// Modified for FarSightVNC on 2026-10-29, 2026-11-01, 2026-11-03, 2026-11-04 and 2026-11-07: added <string>; keyboard test seams and physical key tracking; Shift and dead-key workarounds for TightVNC servers; lost Shift key-up; Shift released around AltGr characters.
//

#ifndef __RFBKEYSYM_H__
#define __RFBKEYSYM_H__

#include <string>

#include <map>
#include <vector>
#include "RfbKeySymListener.h"
#include "util/Keymap.h"
#include "log-writer/LogWriter.h"

// Translates pressed key to a series of rfb symbols. Gives the series
// to out by the listener function serial calling.
class RfbKeySym
{
public:
  RfbKeySym(RfbKeySymListener *extKeySymListener, LogWriter *log);
  virtual ~RfbKeySym();

  // This function doesn't distinguish between left and right modifiers.
  void sendModifier(unsigned char virtKey, bool down);

  void processKeyEvent(unsigned short virtKey, unsigned int addKeyData);
  bool vkCodeToString(unsigned short virtKey, unsigned int scanCode, std::wstring *res);
  void processCharEvent(WCHAR charCode, unsigned int addKeyData);
  // Checks a new modifiers state after focus restoration and sends difference
  void processFocusRestoration();
  // This function release all modifiers unconditionally.
  void processFocusLoss();

  void sendCtrlAltDel();

  // Test seams. By default the layout of the calling thread and the real
  // Caps Lock / Scroll Lock toggle state are used.
  void setKeyboardLayout(HKL layout);
  void setToggleKeysOverride(bool capsLockOn, bool scrollLockOn);
  // Key state table (GetKeyboardState() format) used instead of GetKeyState().
  void setKeyStateOverride(const unsigned char *keys);

  // Set function for m_winKeyIgnore.
  void setWinKeyIgnore(bool winIgnore);

private:
  void clearKeyState();

  void releaseMeta();
  void restoreMeta();
  void releaseModifier(unsigned char modifier);
  void restoreModifier(unsigned char modifier);
  void releaseModifiers();
  void restoreModifiers();

  bool isPressed(unsigned char virtKey);
  HKL getKeyboardLayout();
  bool isToggled(int virtKey);

  // This function does distinguish between a right or left modifier and
  // if the virtKey is a modifier (e.g VK_CONTROL), the function always return
  // a left- or right-hand virtual key value (e.g. VK_LCONTROL or VK_RCONTROL)
  // by taking into account the isRightHint flag.
  // If virtKey is not a modifier, the function returns virtKey value without
  // changing.
  unsigned char distinguishLeftRightModifier(unsigned char virtKey,
                                             bool isRightHint);

  // Checks virtKey state with the server side state and sends difference
  void checkAndSendDiff(unsigned char virtKey, unsigned char state);

  // Send one key event (Alt translated to Meta if Scroll Lock is on).
  virtual void sendKeySymEvent(unsigned int rfbKeySym, bool down);

  // Send one key event (Alt not translated to Meta).
  virtual void sendVerbatimKeySymEvent(unsigned int rfbKeySym, bool down);

  // helper functions
  WCHAR GettingCharFromCtrlSymbol(WCHAR ctrlSymbol);
  // E.g if pressed Ctrl + Alt + A
  // Try found char without modificators
  bool TryTranslateNotPrintableToUnicode(unsigned short virtKey, unsigned int scanCode, HKL currentLayout, WCHAR *unicodeChar);
  // True when Shift+virtKey is a dead key in the current layout.
  bool shiftVariantIsDeadKey(unsigned short virtKey, unsigned int scanCode);
  // True when Shift is held on the server and a TightVNC server would type
  // one of the symbols with Ctrl and/or Alt (AltGr) in the viewer's layout.
  bool needsShiftReleased(const std::vector<UINT32> &symbols);
  // Sends the key-down of the symbols, with Shift released around them when
  // needsShiftReleased().
  void sendCharKeySyms(const std::vector<UINT32> &symbols);
  // Releases the Shift keys that are pressed for the server but not held.
  void releaseShiftsNotHeld();
  bool isKeyHeld(unsigned char virtKey);

  RfbKeySymListener *m_extKeySymListener;

  // This state doesn't difference between left and right modifiers. It's
  // needed to ToUnicodeEx().
  unsigned char m_viewerKeyState[256];

  // This state does difference between left and right modifiers. It's
  // needed to know the server side state (e.g. to release or restore
  // modifiyers state outside from a real key event).
  unsigned char m_serverKeyState[256];
  bool m_leftMetaIsPressed;
  bool m_rightMetaIsPressed;

  Keymap m_keyMap;
  bool m_allowProcessCharEvent;
  bool m_allowProcessDoubleChar;
  bool m_doubleDeadCatched;

  LogWriter *m_log;

  // Flag for ignoring win key.
  bool m_winKeyIgnore;

  // Test seams (see setKeyboardLayout()).
  HKL m_layoutOverride;
  bool m_toggleKeysOverridden;
  bool m_capsLockOverride;
  bool m_scrollLockOverride;
  const unsigned char *m_keyStateOverride;
  std::map<unsigned int, std::vector<UINT32> > m_pressedKeys;
  // Bit 0: Left Shift, bit 1: Right Shift, as sent to sendKeySymEvent().
  unsigned char m_shiftKeysDown;
};

#endif // __RFBKEYSYM_H__
