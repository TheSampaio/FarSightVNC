// Copyright (C) 2026 FarSightVNC contributors.
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

// Deterministic keyboard test for RfbKeySym. It never reads or injects real
// desktop input: it feeds synthetic WM_KEYDOWN/WM_KEYUP data (virtual key,
// scan code, extended flag) to RfbKeySym::processKeyEvent() and records the
// RFB keysyms. The harness emulates what TranslateMessage() does in the
// viewer's message loop (it translates the key down with ToUnicodeEx() before
// the key message is dispatched and then delivers the resulting WM_CHARs), so
// dead keys can be tested without a window.
//
// Keyboard layouts are loaded only for translation (never activated) and the
// ones that were not loaded before are unloaded at exit.

#include "rfb/RfbKeySym.h"
#include "win-system/WinHooks.h"
#include "log-writer/LogWriter.h"
#include "util/Keymap.h"

#include <stdio.h>
#include <string>
#include <vector>
#include <set>

#define XK_MISCELLANY
#include "rfb/keysymdef.h"

namespace {

const unsigned SC_LSHIFT = 0x2A;
const unsigned SC_RSHIFT = 0x36;
const unsigned SC_CTRL = 0x1D;
const unsigned SC_ALT = 0x38;
const unsigned SC_SPACE = 0x39;
const unsigned SC_ABNT_C1 = 0x73;
const unsigned VK_ABNT_C1_CODE = 0xC1;

int g_passed = 0;
int g_failed = 0;

//----------------------------------------------------------------------------
// Event recording

struct Rec {
  UINT32 sym;
  bool down;
  bool operator==(const Rec &o) const { return sym == o.sym && down == o.down; }
};

struct Seq {
  std::vector<Rec> v;
  Seq &d(UINT32 s) { Rec r = {s, true}; v.push_back(r); return *this; }
  Seq &u(UINT32 s) { Rec r = {s, false}; v.push_back(r); return *this; }
  Seq &press(UINT32 s) { return d(s).u(s); }
};

std::string toString(const std::vector<Rec> &v)
{
  std::string s;
  char buf[32];
  for (size_t i = 0; i < v.size(); i++) {
    sprintf_s(buf, "%c%x ", v[i].down ? '+' : '-', v[i].sym);
    s += buf;
  }
  return s.empty() ? "(none) " : s;
}

bool isModifierSym(UINT32 s)
{
  return s >= XK_Shift_L && s <= XK_Hyper_R;
}

// Every press must be followed by a release of the same keysym and a
// release must never come without a press.
bool isBalanced(const std::vector<Rec> &v)
{
  std::vector<std::pair<UINT32, int> > counts;
  for (size_t i = 0; i < v.size(); i++) {
    size_t j = 0;
    while (j < counts.size() && counts[j].first != v[i].sym) j++;
    if (j == counts.size()) counts.push_back(std::make_pair(v[i].sym, 0));
    if (!v[i].down && counts[j].second == 0) return false;
    counts[j].second = v[i].down ? 1 : 0;
  }
  for (size_t j = 0; j < counts.size(); j++) {
    if (counts[j].second != 0) return false;
  }
  return true;
}

class Recorder : public RfbKeySymListener
{
public:
  virtual void onRfbKeySymEvent(unsigned int rfbKeySym, bool down)
  {
    Rec r = {rfbKeySym, down};
    events.push_back(r);
  }
  std::vector<Rec> events;
};

//----------------------------------------------------------------------------
// Keyboard layouts

struct Layout {
  const wchar_t *klid;
  const char *name;
  HKL hkl;
};

Layout g_layouts[] = {
  {L"00010416", "ABNT2",   0},
  {L"00000409", "US",      0},
  {L"00020409", "US-Intl", 0},
  {L"00000407", "DE",      0},
  {L"0000040C", "FR",      0},
  {L"00000816", "PT-PT",   0},
};
const size_t LAYOUT_COUNT = sizeof(g_layouts) / sizeof(g_layouts[0]);
std::vector<HKL> g_loadedByUs;
Layout *g_abnt2 = &g_layouts[0];
Layout *g_us = &g_layouts[1];

std::set<HKL> currentLayoutList()
{
  std::set<HKL> res;
  int n = GetKeyboardLayoutList(0, 0);
  std::vector<HKL> list(n > 0 ? n : 1);
  n = GetKeyboardLayoutList(n, &list[0]);
  for (int i = 0; i < n; i++) res.insert(list[i]);
  return res;
}

void unloadLayouts()
{
  for (size_t i = 0; i < g_loadedByUs.size(); i++) {
    UnloadKeyboardLayout(g_loadedByUs[i]);
  }
  g_loadedByUs.clear();
}

BOOL WINAPI onConsoleCtrl(DWORD)
{
  unloadLayouts();
  return FALSE;
}

bool loadLayouts()
{
  SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
  bool ok = true;
  for (size_t i = 0; i < LAYOUT_COUNT; i++) {
    std::set<HKL> before = currentLayoutList();
    HKL hkl = LoadKeyboardLayoutW(g_layouts[i].klid, KLF_NOTELLSHELL);
    if (hkl == 0) {
      printf("ERROR: cannot load the %s layout (%S)\n", g_layouts[i].name, g_layouts[i].klid);
      ok = false;
      continue;
    }
    g_layouts[i].hkl = hkl;
    if (before.find(hkl) == before.end()) g_loadedByUs.push_back(hkl);
  }
  return ok;
}

//----------------------------------------------------------------------------
// Harness: RfbKeySym + the part of the message loop that matters

class Harness
{
public:
  Harness(HKL hkl, LogWriter *log) : m_hkl(hkl), m_ks(&m_rec, log)
  {
    memset(m_keys, 0, sizeof(m_keys));
    m_ks.setKeyboardLayout(hkl);
    m_ks.setToggleKeysOverride(false, false);
    flushDeadKey();
  }
  ~Harness() { flushDeadKey(); }

  RfbKeySym &ks() { return m_ks; }
  std::vector<Rec> &events() { return m_rec.events; }
  const std::vector<WCHAR> &oracleChars() const { return m_tmChars; }
  HKL hkl() const { return m_hkl; }

  void down(unsigned vk, unsigned scan, bool ext = false) { key(vk, scan, ext, false, false); }
  void repeat(unsigned vk, unsigned scan, bool ext = false) { key(vk, scan, ext, false, true); }
  void up(unsigned vk, unsigned scan, bool ext = false) { key(vk, scan, ext, true, false); }
  void tap(unsigned vk, unsigned scan, bool ext = false) { down(vk, scan, ext); up(vk, scan, ext); }

  // Hook style event: the low level hook reports L/R specific virtual keys.
  void lshiftDown() { down(VK_SHIFT, SC_LSHIFT); }
  void lshiftUp() { up(VK_SHIFT, SC_LSHIFT); }
  void rshiftDown() { down(VK_SHIFT, SC_RSHIFT); }
  void rshiftUp() { up(VK_SHIFT, SC_RSHIFT); }

  unsigned scanOf(unsigned vk) const
  {
    return MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, m_hkl);
  }

  // Clears a pending dead key of the (kernel wide) translation buffer.
  void flushDeadKey()
  {
    BYTE zero[256] = {0};
    WCHAR b[8];
    ToUnicodeEx(VK_SPACE, SC_SPACE, zero, b, 8, 0, m_hkl);
  }

private:
  void setKey(unsigned vk, bool down) { m_keys[vk] = down ? 0x80 : 0; }

  void updateState(unsigned vk, unsigned scan, bool ext, bool isDown)
  {
    switch (vk) {
    case VK_SHIFT: setKey(scan == SC_RSHIFT ? VK_RSHIFT : VK_LSHIFT, isDown); break;
    case VK_CONTROL: setKey(ext ? VK_RCONTROL : VK_LCONTROL, isDown); break;
    case VK_MENU: setKey(ext ? VK_RMENU : VK_LMENU, isDown); break;
    default: setKey(vk, isDown);
    }
    m_keys[VK_SHIFT] = (m_keys[VK_LSHIFT] | m_keys[VK_RSHIFT]);
    m_keys[VK_CONTROL] = (m_keys[VK_LCONTROL] | m_keys[VK_RCONTROL]);
    m_keys[VK_MENU] = (m_keys[VK_LMENU] | m_keys[VK_RMENU]);
  }

  void key(unsigned vk, unsigned scan, bool ext, bool isUp, bool isRepeat)
  {
    unsigned lp = 1 | (scan << 16);
    if (ext) lp |= 0x1000000;
    if (isUp || isRepeat) lp |= 0x40000000;
    if (isUp) lp |= 0x80000000;
    updateState(vk, scan, ext, !isUp);
    // Remote keys are dispatched before TranslateMessage (see TvnViewer).
    m_ks.processKeyEvent((unsigned short)vk, lp);
    std::vector<WCHAR> chars;
    if (!isUp) {
      WCHAR buf[20];
      int n = ToUnicodeEx(vk, scan, m_keys, buf, 20, 0, m_hkl);
      for (int i = 0; i < n; i++) {
        chars.push_back(buf[i]);
        if (buf[i] >= 32) m_tmChars.push_back(buf[i]);
      }
    }
    for (size_t i = 0; i < chars.size(); i++) {
      m_ks.processCharEvent(chars[i], lp);
    }
  }

  HKL m_hkl;
  Recorder m_rec;
  RfbKeySym m_ks;
  BYTE m_keys[256];
  std::vector<WCHAR> m_tmChars;
};

//----------------------------------------------------------------------------
// Checks

void finish(const std::string &name, Harness &h, const Seq &expected)
{
  bool same = h.events() == expected.v;
  bool balanced = isBalanced(h.events());
  if (same && balanced) {
    g_passed++;
    printf("PASS  %s\n", name.c_str());
  } else {
    g_failed++;
    printf("FAIL  %s\n        expected: %s\n        actual:   %s%s\n", name.c_str(),
           toString(expected.v).c_str(), toString(h.events()).c_str(),
           balanced ? "" : "  (UNBALANCED press/release)");
  }
}

void checkTrue(const std::string &name, bool cond, const std::string &detail = "")
{
  if (cond) {
    g_passed++;
    printf("PASS  %s\n", name.c_str());
  } else {
    g_failed++;
    printf("FAIL  %s  %s\n", name.c_str(), detail.c_str());
  }
}

UINT32 symOf(WCHAR ch)
{
  Keymap km;
  UINT32 s = 0;
  km.unicodeCharToKeySym(ch, &s);
  return s;
}

std::string label(const Layout &l, const char *what)
{
  return std::string(l.name) + " " + what;
}

// Finds a virtual key that produces 'ch' with or without Shift.
bool findCharKey(HKL hkl, WCHAR ch, unsigned *vk, unsigned *scan, bool *shift)
{
  for (int s = 0; s < 2; s++) {
    BYTE st[256] = {0};
    if (s) { st[VK_SHIFT] = 0x80; st[VK_LSHIFT] = 0x80; }
    for (unsigned k = 0x20; k <= 0xFE; k++) {
      unsigned sc = MapVirtualKeyExW(k, MAPVK_VK_TO_VSC, hkl);
      if (sc == 0) continue;
      WCHAR b[8];
      int n = ToUnicodeEx(k, sc, st, b, 8, 4, hkl);
      if (n == 1 && b[0] == ch) {
        *vk = k; *scan = sc; *shift = s != 0;
        return true;
      }
    }
  }
  return false;
}

// Finds the dead keys of a layout (the spacing accent is returned in 'dead').
struct DeadKey { unsigned vk, scan; bool shift; WCHAR dead; };

std::vector<DeadKey> findDeadKeys(HKL hkl)
{
  std::vector<DeadKey> res;
  std::set<WCHAR> seen;
  for (int s = 0; s < 2; s++) {
    BYTE st[256] = {0};
    if (s) { st[VK_SHIFT] = 0x80; st[VK_LSHIFT] = 0x80; }
    for (unsigned k = 0x20; k <= 0xFE; k++) {
      unsigned sc = MapVirtualKeyExW(k, MAPVK_VK_TO_VSC, hkl);
      if (sc == 0) continue;
      WCHAR b[8];
      int n = ToUnicodeEx(k, sc, st, b, 8, 4, hkl);
      if (n == -1 && seen.insert(b[0]).second) {
        DeadKey dk = {k, sc, s != 0, b[0]};
        res.push_back(dk);
      }
    }
  }
  return res;
}

//----------------------------------------------------------------------------
// H1 / H3: '?' with left and right Shift, both release orders

void testQuestionMark(const Layout &lay, LogWriter *log)
{
  unsigned vk, scan; bool shift;
  if (!findCharKey(lay.hkl, L'?', &vk, &scan, &shift) || !shift) {
    checkTrue(label(lay, "'?' key is Shift+key"), false, "key not found");
    return;
  }
  if (&lay == g_abnt2) {
    // ABNT2: the extra key left of Right Shift.
    checkTrue("ABNT2 '?' is VK_ABNT_C1 scan 0x73", vk == VK_ABNT_C1_CODE && scan == SC_ABNT_C1);
  }
  UINT32 q = symOf(L'?');
  for (int right = 0; right < 2; right++) {
    for (int keyFirst = 0; keyFirst < 2; keyFirst++) {
      Harness h(lay.hkl, log);
      // Both Shift keys reach the server as Shift_L (TightVNC servers).
      UINT32 sh = XK_Shift_L;
      right ? h.rshiftDown() : h.lshiftDown();
      h.down(vk, scan);
      Seq e;
      e.d(sh).d(q);
      if (keyFirst) {
        h.up(vk, scan);
        right ? h.rshiftUp() : h.lshiftUp();
        e.u(q).u(sh);
      } else {
        right ? h.rshiftUp() : h.lshiftUp();
        h.up(vk, scan);
        e.u(sh).u(q);
      }
      std::string n = label(lay, right ? "H1/H3 '?' RShift" : "H1/H3 '?' LShift");
      n += keyFirst ? ", key released first" : ", Shift released first";
      finish(n, h, e);
    }
  }
}

//----------------------------------------------------------------------------
// H3: overlapping shifts, modifier autorepeat

void testShifts(const Layout &lay, LogWriter *log)
{
  unsigned vk, scan; bool shift;
  findCharKey(lay.hkl, L'?', &vk, &scan, &shift);
  UINT32 q = symOf(L'?');
  {
    Harness h(lay.hkl, log);
    h.lshiftDown(); h.rshiftDown(); h.lshiftUp();
    h.down(vk, scan);       // RShift is still held: must be '?'
    h.up(vk, scan);
    h.rshiftUp();
    Seq e;
    e.d(XK_Shift_L).d(q).u(q).u(XK_Shift_L);
    finish(label(lay, "H3 LShift+RShift, release LShift, '?' still shifted"), h, e);
  }
  {
    Harness h(lay.hkl, log);
    h.rshiftDown(); h.lshiftDown(); h.rshiftUp();
    h.down(vk, scan);
    h.up(vk, scan);
    h.lshiftUp();
    Seq e;
    e.d(XK_Shift_L).d(q).u(q).u(XK_Shift_L);
    finish(label(lay, "H3 RShift+LShift, release RShift, '?' still shifted"), h, e);
  }
}

void testModifierAutorepeat(const Layout &lay, LogWriter *log)
{
  Harness h(lay.hkl, log);
  h.lshiftDown(); h.repeat(VK_SHIFT, SC_LSHIFT); h.repeat(VK_SHIFT, SC_LSHIFT);
  h.lshiftUp();
  Seq e;
  e.d(XK_Shift_L).u(XK_Shift_L);
  finish(label(lay, "H3 modifier autorepeat is sent once"), h, e);
}

void testHookStyleModifiers(const Layout &lay, LogWriter *log)
{
  // The low level hook reports VK_LSHIFT/VK_RSHIFT/VK_RCONTROL... directly,
  // and the system copy (VK_SHIFT) of the same physical key may follow.
  unsigned vk, scan; bool shift;
  findCharKey(lay.hkl, L'?', &vk, &scan, &shift);
  UINT32 q = symOf(L'?');
  Harness h(lay.hkl, log);
  h.down(VK_RSHIFT, SC_RSHIFT);   // posted by the hook
  h.down(vk, scan);               // posted by the hook
  h.up(vk, scan);
  h.down(VK_SHIFT, SC_RSHIFT);    // system copy of the Shift press
  h.up(VK_SHIFT, SC_RSHIFT);      // system copy of the Shift release
  Seq e;
  e.d(XK_Shift_L).d(q).u(q).u(XK_Shift_L);
  finish(label(lay, "H3/H4 hook style RShift with duplicate system copy"), h, e);
}

//----------------------------------------------------------------------------
// H1: plain keys

void testPlainKeys(const Layout &lay, LogWriter *log)
{
  unsigned scanA = MapVirtualKeyExW('A', MAPVK_VK_TO_VSC, lay.hkl);
  {
    Harness h(lay.hkl, log);
    h.down('A', scanA); h.repeat('A', scanA); h.repeat('A', scanA); h.up('A', scanA);
    Seq e;
    e.d(symOf(L'a')).d(symOf(L'a')).d(symOf(L'a')).u(symOf(L'a'));
    finish(label(lay, "H1 autorepeat then release"), h, e);
  }
  {
    Harness h(lay.hkl, log);
    h.lshiftDown();
    h.down('A', scanA); h.repeat('A', scanA); h.up('A', scanA);
    h.lshiftUp();
    Seq e;
    e.d(XK_Shift_L).d(symOf(L'A')).d(symOf(L'A')).u(symOf(L'A')).u(XK_Shift_L);
    finish(label(lay, "H1 shifted autorepeat releases the same keysym"), h, e);
  }
  {
    Harness h(lay.hkl, log);
    h.up('A', scanA);            // release without press
    h.lshiftUp();                // release without press
    finish(label(lay, "H1 stray releases are ignored"), h, Seq());
  }
}

//----------------------------------------------------------------------------
// AltGr (Windows sends a fake LCtrl followed by RAlt)

void testAltGr(Harness &h, Seq &e, unsigned vk, unsigned scan, UINT32 sym)
{
  h.down(VK_CONTROL, SC_CTRL, false);
  h.down(VK_MENU, SC_ALT, true);
  h.down(vk, scan);
  h.up(vk, scan);
  h.up(VK_MENU, SC_ALT, true);
  h.up(VK_CONTROL, SC_CTRL, false);
  e.d(XK_Control_L).d(XK_Alt_R);
  e.u(XK_Control_L).u(XK_Alt_R).d(sym).d(XK_Control_L).d(XK_Alt_R);
  e.u(XK_Control_L).u(XK_Alt_R).u(sym).d(XK_Control_L).d(XK_Alt_R);
  e.u(XK_Alt_R).u(XK_Control_L);
}

void testAltGrAbnt2(LogWriter *log)
{
  Harness h(g_abnt2->hkl, log);
  unsigned scanQ = h.scanOf('Q');
  Seq e;
  testAltGr(h, e, 'Q', scanQ, symOf(L'/'));
  finish("ABNT2 AltGr+Q gives '/' (modifiers released around it)", h, e);
}

void testCtrlLetter(const Layout &lay, LogWriter *log)
{
  unsigned scanC = MapVirtualKeyExW('C', MAPVK_VK_TO_VSC, lay.hkl);
  {
    Harness h(lay.hkl, log);
    h.down(VK_CONTROL, SC_CTRL); h.tap('C', scanC); h.up(VK_CONTROL, SC_CTRL);
    Seq e;
    e.d(XK_Control_L).d(symOf(L'c')).u(symOf(L'c')).u(XK_Control_L);
    finish(label(lay, "Ctrl+C sends 'c'"), h, e);
  }
  {
    Harness h(lay.hkl, log);
    h.down(VK_CONTROL, SC_CTRL, true);   // right Ctrl
    h.lshiftDown();
    h.tap('C', scanC);
    h.lshiftUp();
    h.up(VK_CONTROL, SC_CTRL, true);
    Seq e;
    e.d(XK_Control_R).d(XK_Shift_L).d(symOf(L'C')).u(symOf(L'C')).u(XK_Shift_L).u(XK_Control_R);
    finish(label(lay, "RCtrl+Shift+C sends 'C'"), h, e);
  }
}

//----------------------------------------------------------------------------
// H2: dead keys

void tapDead(Harness &h, const DeadKey &dk)
{
  if (dk.shift) h.lshiftDown();
  h.down(dk.vk, dk.scan);
  h.up(dk.vk, dk.scan);
  if (dk.shift) h.lshiftUp();
}

// The expected output is the text the system produces for the same keys.
void finishText(const std::string &name, Harness &h)
{
  Seq e;
  const std::vector<WCHAR> &chars = h.oracleChars();
  for (size_t i = 0; i < chars.size(); i++) e.press(symOf(chars[i]));
  std::vector<Rec> actual;
  for (size_t i = 0; i < h.events().size(); i++) {
    if (!isModifierSym(h.events()[i].sym)) actual.push_back(h.events()[i]);
  }
  bool same = actual == e.v;
  bool balanced = isBalanced(h.events());
  if (same && balanced && !chars.empty()) {
    g_passed++;
    printf("PASS  %s\n", name.c_str());
  } else {
    g_failed++;
    printf("FAIL  %s\n        expected: %s\n        actual:   %s%s\n", name.c_str(),
           toString(e.v).c_str(), toString(h.events()).c_str(),
           balanced ? "" : "  (UNBALANCED press/release)");
  }
}

void testDeadKeys(const Layout &lay, LogWriter *log)
{
  std::vector<DeadKey> dead = findDeadKeys(lay.hkl);
  if (dead.empty()) return;
  unsigned scanA = MapVirtualKeyExW('A', MAPVK_VK_TO_VSC, lay.hkl);
  for (size_t i = 0; i < dead.size() && i < 4; i++) {
    const DeadKey &dk = dead[i];
    char tag[64];
    sprintf_s(tag, "H2 dead U+%04X", (unsigned)dk.dead);
    {
      Harness h(lay.hkl, log);
      tapDead(h, dk); h.tap('A', scanA);
      finishText(label(lay, (std::string(tag) + " + a").c_str()), h);
    }
    {
      Harness h(lay.hkl, log);
      tapDead(h, dk); h.tap(VK_SPACE, SC_SPACE);
      finishText(label(lay, (std::string(tag) + " + space").c_str()), h);
    }
    {
      Harness h(lay.hkl, log);
      tapDead(h, dk); tapDead(h, dk);
      finishText(label(lay, (std::string(tag) + " twice").c_str()), h);
    }
    {
      Harness h(lay.hkl, log);
      tapDead(h, dk); tapDead(h, dk); h.tap('A', scanA);
      finishText(label(lay, (std::string(tag) + " twice, then plain a").c_str()), h);
    }
    {
      // The key release must not alter the pending dead key of the system.
      Harness h(lay.hkl, log);
      if (dk.shift) h.lshiftDown();
      h.down(dk.vk, dk.scan);
      h.up(dk.vk, dk.scan);
      if (dk.shift) h.lshiftUp();
      BYTE zero[256] = {0};
      WCHAR b[8];
      int n = ToUnicodeEx('A', scanA, zero, b, 8, 4, lay.hkl);
      checkTrue(label(lay, (std::string(tag) + " pending after press+release").c_str()),
                n == 1 && b[0] != L'a', "dead key state was lost or consumed");
    }
  }
}

//----------------------------------------------------------------------------
// Ctrl+Alt+Del button and Win key

void testCtrlAltDel(LogWriter *log)
{
  {
    Harness h(g_us->hkl, log);
    h.ks().sendCtrlAltDel();
    Seq e;
    e.d(XK_Control_L).d(XK_Alt_L).d(XK_Delete).u(XK_Delete).u(XK_Alt_L).u(XK_Control_L);
    finish("Ctrl+Alt+Del button, nothing held", h, e);
  }
  {
    Harness h(g_us->hkl, log);
    h.rshiftDown();
    h.ks().sendCtrlAltDel();
    h.rshiftUp();
    Seq e;
    e.d(XK_Shift_L);
    e.u(XK_Shift_L);
    e.d(XK_Control_L).d(XK_Alt_L).d(XK_Delete).u(XK_Delete).u(XK_Alt_L).u(XK_Control_L);
    e.d(XK_Shift_L);
    e.u(XK_Shift_L);
    finish("Ctrl+Alt+Del button releases and restores a held Right Shift", h, e);
  }
}

void testWinKey(LogWriter *log)
{
  {
    Harness h(g_us->hkl, log);
    h.tap(VK_LWIN, 0x5B, true);
    h.tap(VK_RWIN, 0x5C, true);
    finish("Win key is ignored by default", h, Seq());
  }
  {
    Harness h(g_us->hkl, log);
    h.ks().setWinKeyIgnore(false);
    h.tap(VK_LWIN, 0x5B, true);
    Seq e;
    e.press(XK_Super_L);
    finish("Win key is sent when not ignored", h, e);
  }
  {
    // Leaving fullscreen (hook off, Win ignored again) while Win is held.
    Harness h(g_us->hkl, log);
    h.ks().setWinKeyIgnore(false);
    h.down(VK_LWIN, 0x5B, true);
    h.ks().setWinKeyIgnore(true);
    h.up(VK_LWIN, 0x5B, true);
    Seq e;
    e.d(XK_Super_L).u(XK_Super_L);
    finish("R1 held Win key is released after the ignore flag is set", h, e);
  }
}

//----------------------------------------------------------------------------
// TightVNC server workarounds (2026-11-03)

void testDigitWithDeadShiftVariant(LogWriter *log)
{
  unsigned scan6 = MapVirtualKeyExW('6', MAPVK_VK_TO_VSC, g_abnt2->hkl);
  {
    Harness h(g_abnt2->hkl, log);
    h.tap('6', scan6);
    Seq e;
    e.press(XK_KP_6);
    finish("ABNT2 plain 6 (Shift+6 is dead) is sent as KP_6", h, e);
  }
  {
    Harness h(g_abnt2->hkl, log);
    h.down('6', scan6); h.repeat('6', scan6); h.up('6', scan6);
    Seq e;
    e.d(XK_KP_6).d(XK_KP_6).u(XK_KP_6);
    finish("ABNT2 autorepeat 6 keeps KP_6", h, e);
  }
  {
    Harness h(g_abnt2->hkl, log);
    unsigned scan5 = MapVirtualKeyExW('5', MAPVK_VK_TO_VSC, g_abnt2->hkl);
    h.tap('5', scan5);
    Seq e;
    e.press(symOf(L'5'));
    finish("ABNT2 plain 5 (Shift+5 is not dead) stays 5", h, e);
  }
  {
    Harness h(g_us->hkl, log);
    h.tap('6', MapVirtualKeyExW('6', MAPVK_VK_TO_VSC, g_us->hkl));
    Seq e;
    e.press(symOf(L'6'));
    finish("US plain 6 stays 6", h, e);
  }
  {
    Harness h(g_layouts[2].hkl, log);
    h.tap('6', MapVirtualKeyExW('6', MAPVK_VK_TO_VSC, g_layouts[2].hkl));
    Seq e;
    e.press(XK_KP_6);
    finish("US-Intl plain 6 (Shift+6 is dead) is sent as KP_6", h, e);
  }
  {
    Harness h(g_abnt2->hkl, log);
    h.down(VK_CONTROL, SC_CTRL);
    h.tap('6', scan6);
    h.up(VK_CONTROL, SC_CTRL);
    Seq e;
    e.d(XK_Control_L).press(symOf(L'6')).u(XK_Control_L);
    finish("ABNT2 Ctrl+6 stays 6", h, e);
  }
}

void testRightShiftIsShiftL(LogWriter *log)
{
  Harness h(g_abnt2->hkl, log);
  unsigned scanA = MapVirtualKeyExW('A', MAPVK_VK_TO_VSC, g_abnt2->hkl);
  h.rshiftDown();
  h.tap('A', scanA);
  h.rshiftUp();
  h.lshiftDown(); h.rshiftDown(); h.rshiftUp(); h.lshiftUp();
  Seq e;
  e.d(XK_Shift_L).press(symOf(L'A')).u(XK_Shift_L);
  e.d(XK_Shift_L).u(XK_Shift_L);
  finish("ABNT2 Right Shift is sent as Shift_L, overlapping Shifts once", h, e);
}

//----------------------------------------------------------------------------
// H5: focus loss

void testFocusLoss(LogWriter *log)
{
  unsigned scanA = MapVirtualKeyExW('A', MAPVK_VK_TO_VSC, g_us->hkl);
  {
    Harness h(g_us->hkl, log);
    h.lshiftDown();
    h.down('A', scanA);
    h.ks().processFocusLoss();
    h.up('A', scanA);          // the release is lost with the focus
    Seq e;
    e.d(XK_Shift_L).d(symOf(L'A'));
    e.u(symOf(L'A')).u(XK_Shift_L);
    finish("H5 focus loss releases the held key and Shift", h, e);
  }
  {
    Harness h(g_us->hkl, log);
    h.down(VK_LEFT, 0x4B, true);
    h.down(VK_F5, 0x3F);
    h.ks().processFocusLoss();
    Seq e;
    e.d(XK_Left).d(XK_F5);
    e.u(XK_Left).u(XK_F5);
    std::vector<Rec> sorted = h.events();
    // Order of the key releases is not important, balance is.
    bool ok = isBalanced(sorted) && sorted.size() == 4;
    checkTrue("H5 focus loss releases every held special key", ok, toString(sorted));
  }
  {
    Harness h(g_us->hkl, log);
    h.down('A', scanA);
    h.ks().processFocusLoss();
    h.ks().processFocusLoss();   // second loss is a no-op
    Seq e;
      e.d(symOf(L'a')).u(symOf(L'a'));
    finish("H5 focus loss twice releases once", h, e);
  }
}

//----------------------------------------------------------------------------
// H4: low level hook helpers

void testHook()
{
  const DWORD EXT = LLKHF_EXTENDED;
  checkTrue("H4 lParam keeps the whole scan code (0x73)",
            (UINT)WinHooks::makeKeyMessageLParam(0x73, 0) == (1u | (0x73u << 16)));
  checkTrue("H4 lParam keeps Right Shift scan code (0x36)",
            (UINT)WinHooks::makeKeyMessageLParam(0x36, 0) == (1u | (0x36u << 16)));
  checkTrue("H4 lParam extended bit",
            (UINT)WinHooks::makeKeyMessageLParam(0x4B, EXT) == (1u | (0x4Bu << 16) | 0x1000000u));
  checkTrue("H4 lParam context bit for Alt",
            (UINT)WinHooks::makeKeyMessageLParam(0x1E, LLKHF_ALTDOWN) == (1u | (0x1Eu << 16) | 0x20000000u));
  checkTrue("H4 lParam release has previous-state and transition bits",
            (UINT)WinHooks::makeKeyMessageLParam(0x73, LLKHF_UP) == (1u | (0x73u << 16) | 0x40000000u | 0x80000000u));

  checkTrue("H4 ordinary key is posted and swallowed",
            WinHooks::decideKeyAction('A', 0) == WinHooks::HOOK_KEY_POST);
  checkTrue("H4 ordinary key release is posted and swallowed",
            WinHooks::decideKeyAction('A', LLKHF_UP) == WinHooks::HOOK_KEY_POST);
  checkTrue("H4 Shift press shares the posted queue",
            WinHooks::decideKeyAction(VK_LSHIFT, 0) == WinHooks::HOOK_KEY_POST);
  checkTrue("H4 Right Ctrl press shares the posted queue",
            WinHooks::decideKeyAction(VK_RCONTROL, EXT) == WinHooks::HOOK_KEY_POST);
  checkTrue("H4 Alt press shares the posted queue",
            WinHooks::decideKeyAction(VK_RMENU, EXT | LLKHF_ALTDOWN) == WinHooks::HOOK_KEY_POST);
  checkTrue("H4 modifier release shares the posted queue",
            WinHooks::decideKeyAction(VK_LSHIFT, LLKHF_UP) == WinHooks::HOOK_KEY_POST);
  checkTrue("H4 Caps Lock is left to the system",
            WinHooks::decideKeyAction(VK_CAPITAL, 0) == WinHooks::HOOK_KEY_PASS);
  checkTrue("H4 injected key is not swallowed",
            WinHooks::decideKeyAction('A', LLKHF_INJECTED) == WinHooks::HOOK_KEY_PASS);
  checkTrue("H4 injected key release is not swallowed",
            WinHooks::decideKeyAction('A', LLKHF_INJECTED | LLKHF_UP) == WinHooks::HOOK_KEY_PASS);
}

//----------------------------------------------------------------------------
// R2: key state table of hook-posted keys, R3: hook ownership

void testStateTable()
{
  BYTE k[256] = {0};
  k[VK_CAPITAL] = 1;
  unsigned char r = WinHooks::applyKeyToStateTable(k, VK_SHIFT, 1u | (SC_RSHIFT << 16));
  checkTrue("R2 Right Shift (scan 0x36) sets VK_RSHIFT and VK_SHIFT",
            r == VK_RSHIFT && k[VK_RSHIFT] == 0x80 && k[VK_LSHIFT] == 0 && k[VK_SHIFT] == 0x80);
  WinHooks::applyKeyToStateTable(k, VK_LSHIFT, 1u | (SC_LSHIFT << 16));
  WinHooks::applyKeyToStateTable(k, VK_RSHIFT, 1u | (SC_RSHIFT << 16) | 0xC0000000u);
  checkTrue("R2 releasing one Shift keeps the generic Shift down", k[VK_SHIFT] == 0x80 && k[VK_RSHIFT] == 0);
  WinHooks::applyKeyToStateTable(k, VK_LSHIFT, 1u | (SC_LSHIFT << 16) | 0xC0000000u);
  checkTrue("R2 releasing both Shifts clears the generic Shift and keeps Caps Lock toggle",
            k[VK_SHIFT] == 0 && k[VK_CAPITAL] == 1);
  r = WinHooks::applyKeyToStateTable(k, VK_MENU, 1u | (SC_ALT << 16) | 0x1000000u);
  checkTrue("R2 extended Alt is Right Alt (AltGr)", r == VK_RMENU && k[VK_RMENU] == 0x80 && k[VK_MENU] == 0x80);
  r = WinHooks::applyKeyToStateTable(k, VK_LCONTROL, 1u | (SC_CTRL << 16));
  checkTrue("R2 AltGr plus its Left Ctrl keep both tables set",
            r == VK_LCONTROL && k[VK_CONTROL] == 0x80 && k[VK_MENU] == 0x80);
}

class DummyListener : public HookEventListener
{
public:
  virtual LRESULT onHookProc(int, WPARAM, LPARAM) { return 0; }
};

void testHookOwnership()
{
  DummyListener a, b;
  // The hook is installed only for the duration of this check and this thread
  // pumps no input meanwhile; the listeners never swallow keys.
  WinHooks::registerKeyboardHook(&a);
  checkTrue("R3 hook registered for A", WinHooks::isRegisteredFor(&a));
  WinHooks::unregisterKeyboardHook(&b);
  checkTrue("R3 another window cannot remove the hook of A", WinHooks::isRegisteredFor(&a));
  WinHooks::registerKeyboardHook(&b);
  checkTrue("R3 a newly active window takes the hook over",
            WinHooks::isRegisteredFor(&b) && !WinHooks::isRegisteredFor(&a));
  WinHooks::unregisterKeyboardHook(&a);
  checkTrue("R3 the previous owner cannot remove it either", WinHooks::isRegisteredFor(&b));
  {
    WinHooks unrelated;
  }
  checkTrue("R3 destroying a WinHooks object keeps the hook of its owner", WinHooks::isRegisteredFor(&b));
  WinHooks::unregisterKeyboardHook(&b);
  checkTrue("R3 the owner removes the hook", !WinHooks::isRegisteredFor(&b));
  WinHooks::unregisterKeyboardHook(&b);   // second call is harmless
}

void testSyntheticModifierFocus(LogWriter *log)
{
  Harness h(g_us->hkl, log);
  h.ks().sendModifier(VK_RSHIFT, true);
  h.ks().processFocusLoss();
  h.ks().processFocusLoss();
  Seq e;
  e.d(XK_Shift_L).u(XK_Shift_L);
  finish("toolbar modifier is released exactly once on focus loss", h, e);

  Harness meta(g_us->hkl, log);
  meta.ks().setToggleKeysOverride(false, true);
  meta.down(VK_MENU, SC_ALT);
  meta.ks().setToggleKeysOverride(false, false);
  meta.up(VK_MENU, SC_ALT);
  Seq expected;
  expected.d(XK_Meta_L).u(XK_Meta_L);
  finish("Alt release preserves Meta when Scroll Lock changes", meta, expected);
}
} // namespace

int main()
{
  LogWriter log(0);
  if (!loadLayouts()) {
    unloadLayouts();
    return 2;
  }

  for (size_t i = 0; i < LAYOUT_COUNT; i++) {
    const Layout &lay = g_layouts[i];
    testQuestionMark(lay, &log);
    testShifts(lay, &log);
    testModifierAutorepeat(lay, &log);
    testHookStyleModifiers(lay, &log);
    testPlainKeys(lay, &log);
    testCtrlLetter(lay, &log);
    testDeadKeys(lay, &log);
  }
  testAltGrAbnt2(&log);
  testDigitWithDeadShiftVariant(&log);
  testRightShiftIsShiftL(&log);
  testCtrlAltDel(&log);
  testWinKey(&log);
  testFocusLoss(&log);
  testHook();
  testStateTable();
  testHookOwnership();
  testSyntheticModifierFocus(&log);

  unloadLayouts();
  printf("\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
