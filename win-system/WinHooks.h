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
// Modified for FarSightVNC on 2026-11-16: key capture hook (Settings, Add key...) that takes the next key-down and swallows it.
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
  // A local key is matched as the bare key: the modifiers the viewer owns
  // were swallowed by the hook, so Ctrl+Numpad Minus does not fire a local
  // hot key and the server still gets the Ctrl. Only the unmodified key is
  // guaranteed to be local. Numpad digits depend on Num Lock: captured with
  // Num Lock on they are VK_NUMPAD0-9; with Num Lock off the same keys
  // arrive as navigation keys (Insert, End...) and are not local.
  typedef std::bitset<512> LocalKeySet;
  static unsigned localKeyIndex(DWORD vkCode, bool extended);
  // Stored form: comma separated hexadecimal virtual keys, exactly two
  // digits each, "e" after the two digits of an extended key ("6D,2De").
  // Invalid entries (a lone "e", one digit...) are ignored, duplicates
  // merged.
  static LocalKeySet parseLocalKeys(const TCHAR *text);
  static StringStorage formatLocalKeys(const LocalKeySet &keys);
  // Local keys of the hook and of the viewer windows. May be called from any
  // UI thread: the set is stored as atomic words that the hook reads
  // without a lock.
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
    // A local key-down goes to Windows. Modifiers the viewer owns were
    // swallowed, so Windows sees the bare key (see LocalKeySet). A key-up
    // still follows its key-down, so a key held while the local keys change
    // is released where it was pressed.
    // The owner is kept per localKeyIndex (virtual key plus the extended
    // flag of flags): Enter and Numpad Enter, Numpad 0 and Insert have
    // separate owners, like their local key decisions.
    HookKeyAction route(DWORD vkCode, DWORD flags, bool viewerOwnsKeyboard,
                        bool systemHasKeyDown);
    // Not cleared by reset().
    void setLocalKeys(const LocalKeySet &keys);
    bool isLocalKey(DWORD vkCode, bool extended) const;
  private:
    KeyRouter(const KeyRouter &);
    KeyRouter &operator=(const KeyRouter &);
    enum Owner { OWNER_NONE, OWNER_VIEWER, OWNER_SYSTEM };
    // Indexed by localKeyIndex(). Only the hook thread touches it.
    unsigned char m_owner[512];
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

  // One-shot capture of the next key pressed, for the "Add key..." button of
  // the Local shortcuts. A local tool (Lightshot, the Snipping Tool) fires its
  // own hot key before a window ever sees the key, so the key cannot be read
  // from window messages. While armed, a dedicated WH_KEYBOARD_LL hook takes
  // the next key-down and swallows it: the key reaches neither Windows, nor the
  // hot key table, nor any other application. Its autorepeats and its key-up
  // are swallowed too, then the hook removes itself.
  // The hook is independent of the session hook (registerKeyboardHook) and of
  // KeyRouter: it can be armed with no session, and a swallowed key never
  // reaches the router. A key held when the capture is armed (the Enter that
  // pressed the button) and injected keys are never taken.
  // The hook runs on the thread that armed it, which must pump messages. The
  // hook only posts a message; nothing else is done inside it.
  // A global keyboard hook must never outlive its purpose: the object removes
  // it in its destructor, and arm() sets a timeout.
  class KeyCapture
  {
  public:
    // What the hook does with a key event.
    enum Action {
      CAPTURE_PASS,       // Not ours: Windows and the next hook see it.
      CAPTURE_SWALLOW,    // The taken key again (autorepeat, key-up).
      CAPTURE_TAKE        // The key captured: swallowed and reported once.
    };

    // Pure state of the capture (unit-testable). systemHasKeyDown: Windows
    // already has the key down (GetAsyncKeyState inside the hook, which
    // reports the state before the current event).
    class Filter
    {
    public:
      Filter();
      void reset();
      Action filter(DWORD vkCode, DWORD flags, bool systemHasKeyDown);
      // The key-up of the taken key was swallowed: nothing is left to do.
      bool isFinished() const { return m_state == FINISHED; }
    private:
      enum State { WAITING, TAKEN, FINISHED };
      State m_state;
      unsigned m_taken;   // localKeyIndex of the taken key.
    };

    KeyCapture();
    // Removes the hook if this object armed it.
    ~KeyCapture();

    // Installs the hook. The key is posted once to target as
    // PostMessage(target, message, vk, extended ? 1 : 0). The capture ends
    // without a key after timeoutMs: the hook is removed and the message is
    // posted with vk 0. A target of 0 posts to the thread queue. Re-arming an
    // armed object restarts it. Returns false (nothing installed) when the hook
    // cannot be installed or another object has the capture armed.
    bool arm(HWND target, UINT message, DWORD timeoutMs);
    // Removes the hook and the timeout; harmless when not armed.
    void disarm();
    // True while the hook of this object is installed (it stays installed
    // after the key-down until the key-up of the taken key).
    bool isArmed() const;
    // True when a capture hook is installed, whoever owns it.
    static bool isHookInstalled();

    // Hook procedure (public like lowLevelKeyboardHook, for the tests).
    static LRESULT CALLBACK hookProc(int nCode, WPARAM wParam, LPARAM lParam);

  private:
    KeyCapture(const KeyCapture &);
    KeyCapture &operator=(const KeyCapture &);
    static VOID CALLBACK timerProc(HWND, UINT, UINT_PTR, DWORD);
    // Removes the hook and the timer of whichever object armed them.
    static void release();

    static HHOOK s_hook;
    static UINT_PTR s_timer;
    static HWND s_target;
    static UINT s_message;
    static const KeyCapture *s_owner;
    static Filter s_filter;
  };

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
