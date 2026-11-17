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
// Deterministic tests of util/WindowsFileName: the reserved DOS device names,
// the rule for a single name component, GetFullPathName canonicalization and
// the containment check that stops directory traversal. These are pure
// functions over strings: no file is created, written or deleted.
//

#include "viewer-security-test/SecurityTestHarness.h"

#include "util/WindowsFileName.h"

namespace {

bool reserved(const TCHAR *name)
{
  return WindowsFileName::isReservedDosDeviceName(name, _tcslen(name));
}

bool safe(const TCHAR *name)
{
  return WindowsFileName::isSafeNameComponent(name, _tcslen(name));
}

bool inside(const TCHAR *candidate, const TCHAR *directory)
{
  return WindowsFileName::isPathInsideDirectory(candidate, directory);
}

//----------------------------------------------------------------------------
// The reserved DOS device names.

void testReservedDeviceNames(SecurityTest::Counters *c)
{
  static const TCHAR *const DEVICES[] = {
    _T("CON"), _T("PRN"), _T("AUX"), _T("NUL"),
    _T("COM1"), _T("COM2"), _T("COM3"), _T("COM4"), _T("COM5"),
    _T("COM6"), _T("COM7"), _T("COM8"), _T("COM9"),
    _T("LPT1"), _T("LPT2"), _T("LPT3"), _T("LPT4"), _T("LPT5"),
    _T("LPT6"), _T("LPT7"), _T("LPT8"), _T("LPT9")
  };
  for (size_t i = 0; i < sizeof(DEVICES) / sizeof(DEVICES[0]); i++) {
    StringStorage bare(DEVICES[i]);
    StringStorage lower(DEVICES[i]);
    lower.toLowerCase();
    StringStorage withExtension(DEVICES[i]);
    withExtension.appendString(_T(".txt"));
    StringStorage twoExtensions(DEVICES[i]);
    twoExtensions.toLowerCase();
    twoExtensions.appendString(_T(".tar.gz"));

    c->check(reserved(bare.getString()), "device: the bare name is reserved");
    c->check(reserved(lower.getString()),
             "device: the lowercase name is reserved");
    c->check(reserved(withExtension.getString()),
             "device: the name with an extension is reserved");
    c->check(reserved(twoExtensions.getString()),
             "device: the name with two extensions is reserved");
    // A device name must also be refused as a name component.
    c->check(!safe(bare.getString()), "device: the bare name is not safe");
    c->check(!safe(withExtension.getString()),
             "device: the name with an extension is not safe");
  }

  // Names that only begin like a device, and the devices that do not exist.
  c->check(!reserved(_T("CONSOLE")), "device: CONSOLE is an ordinary name");
  c->check(safe(_T("CONSOLE")), "device: CONSOLE is a safe name");
  c->check(!reserved(_T("COM0")), "device: COM0 does not exist");
  c->check(safe(_T("COM0")), "device: COM0 is a safe name");
  c->check(!reserved(_T("COM10")), "device: COM10 does not exist");
  c->check(safe(_T("COM10")), "device: COM10 is a safe name");
  c->check(!reserved(_T("LPT0")), "device: LPT0 does not exist");
  c->check(!reserved(_T("CONS")), "device: CONS is an ordinary name");
  c->check(!reserved(_T("NULL")), "device: NULL is an ordinary name");
  c->check(!reserved(_T("report.txt")), "device: a plain name is not reserved");
  c->check(!reserved(_T("")), "device: an empty name is not reserved");
  // ".CON" starts with the extension separator, so the part before the first
  // '.' is empty and the name is not a device.
  c->check(!reserved(_T(".CON")), "device: .CON is an ordinary name");
}

//----------------------------------------------------------------------------
// A single name component.

void testNameComponentRules(SecurityTest::Counters *c)
{
  c->check(!WindowsFileName::isSafeNameComponent(_T(""), 0),
           "name: an empty component is refused");
  c->check(!safe(_T(".")), "name: \".\" is refused");
  c->check(!safe(_T("..")), "name: \"..\" is refused");
  // "..." is not a directory reference, but it still ends in a dot, which
  // Windows strips.
  c->check(!safe(_T("...")), "name: \"...\" is refused for its trailing dot");

  c->check(!safe(_T("sub\\report.txt")), "name: a backslash is refused");
  c->check(!safe(_T("sub/report.txt")), "name: a forward slash is refused");
  c->check(!safe(_T("C:report.txt")), "name: a colon is refused");
  c->check(!safe(_T("report.txt:stream")),
           "name: an alternate data stream is refused");

  static const TCHAR FORBIDDEN[] = {
    _T('\\'), _T('/'), _T(':'), _T('<'), _T('>'), _T('"'), _T('|'),
    _T('?'), _T('*')
  };
  for (size_t i = 0; i < sizeof(FORBIDDEN) / sizeof(FORBIDDEN[0]); i++) {
    StringStorage name(_T("rep"));
    name.appendChar(FORBIDDEN[i]);
    name.appendString(_T("ort.txt"));
    c->check(!safe(name.getString()),
             "name: a forbidden character is refused");
    // The same character at the start and at the end of the name.
    StringStorage leading;
    leading.appendChar(FORBIDDEN[i]);
    leading.appendString(_T("report.txt"));
    c->check(!safe(leading.getString()),
             "name: a leading forbidden character is refused");
    StringStorage trailing(_T("report.txt"));
    trailing.appendChar(FORBIDDEN[i]);
    c->check(!safe(trailing.getString()),
             "name: a trailing forbidden character is refused");
  }

  StringStorage control(_T("rep"));
  control.appendChar(static_cast<TCHAR>(0x07));
  control.appendString(_T("ort.txt"));
  c->check(!safe(control.getString()), "name: a control character is refused");
  StringStorage tab(_T("report"));
  tab.appendChar(_T('\t'));
  c->check(!safe(tab.getString()), "name: a tab is refused");
  // A NUL inside the component: only the length is believed, so the character
  // is seen and refused rather than ending the name early.
  const TCHAR embeddedNul[] = { _T('a'), 0, _T('b') };
  c->check(!WindowsFileName::isSafeNameComponent(embeddedNul, 3),
           "name: an embedded NUL is refused");

  c->check(!safe(_T("report.txt ")), "name: a trailing space is refused");
  c->check(!safe(_T("report.txt.")), "name: a trailing dot is refused");
  c->check(!safe(_T(" ")), "name: a single space is refused");
  c->check(safe(_T(" report.txt")),
           "name: a leading space is accepted (Windows keeps it)");

  c->check(safe(_T("report.txt")), "name: a plain name is accepted");
  c->check(safe(_T("My Report (final).v2.tar.gz")),
           "name: spaces and several dots are accepted");
  c->check(safe(_T("rela\u00e7\u00e3o de clientes.pdf")),
           "name: a non-ASCII name is accepted");
  c->check(safe(_T("\u6587\u4ef6.txt")),
           "name: a non-Latin name is accepted");
  c->check(safe(_T("-rf")), "name: a name that looks like a switch is accepted");
  // Only the first length characters are looked at.
  c->check(WindowsFileName::isSafeNameComponent(_T("report.txt*"), 10),
           "name: only the given length is looked at");
}

//----------------------------------------------------------------------------
// Canonicalization.

void testCanonicalize(SecurityTest::Counters *c)
{
  StringStorage out;
  c->check(WindowsFileName::canonicalize(_T("C:\\out\\sub\\..\\a.txt"), &out),
           "canonicalize: an absolute path succeeds");
  c->check(out.isEqualTo(_T("C:\\out\\a.txt")),
           "canonicalize: \"..\" is resolved");

  c->check(WindowsFileName::canonicalize(_T("C:\\out\\.\\a.txt"), &out),
           "canonicalize: \".\" succeeds");
  c->check(out.isEqualTo(_T("C:\\out\\a.txt")), "canonicalize: \".\" is removed");

  c->check(WindowsFileName::canonicalize(_T("C:/out/a.txt"), &out),
           "canonicalize: forward slashes succeed");
  c->check(out.isEqualTo(_T("C:\\out\\a.txt")),
           "canonicalize: forward slashes become backslashes");

  // A path far longer than MAX_PATH must come back whole instead of truncated.
  StringStorage longPath(_T("C:\\out"));
  for (int i = 0; i < 40; i++) {
    longPath.appendString(_T("\\0123456789abcdef0123456789abcdef"));
  }
  size_t longLength = longPath.getLength();
  c->check(WindowsFileName::canonicalize(longPath.getString(), &out),
           "canonicalize: a long path succeeds");
  c->check(out.getLength() == longLength,
           "canonicalize: a long path is not truncated");
  c->check(out.isEqualTo(longPath.getString()),
           "canonicalize: a long path comes back unchanged");

  StringStorage untouched(_T("sentinel"));
  c->check(!WindowsFileName::canonicalize(_T(""), &untouched),
           "canonicalize: an empty path fails");
  c->check(untouched.isEqualTo(_T("sentinel")),
           "canonicalize: the output is untouched on failure");
  c->check(!WindowsFileName::canonicalize(0, &untouched),
           "canonicalize: a null path fails");
}

//----------------------------------------------------------------------------
// Containment: the check that stops directory traversal.

void testPathInsideDirectory(SecurityTest::Counters *c)
{
  const TCHAR *const DIRECTORY = _T("C:\\out");

  c->check(inside(_T("C:\\out\\a.txt"), DIRECTORY),
           "inside: a file in the directory");
  c->check(inside(_T("C:\\out\\sub\\a.txt"), DIRECTORY),
           "inside: a file in a subdirectory");
  c->check(inside(_T("C:\\out\\sub"), DIRECTORY),
           "inside: a subdirectory itself");

  c->check(!inside(_T("C:\\outside\\a.txt"), DIRECTORY),
           "inside: a prefix match without a separator boundary is refused");
  c->check(!inside(_T("C:\\out"), DIRECTORY),
           "inside: the directory itself is not inside itself");
  c->check(!inside(_T("C:\\out\\"), DIRECTORY),
           "inside: the directory with a trailing separator is not inside");
  c->check(!inside(_T("C:\\"), DIRECTORY), "inside: the drive root is refused");
  c->check(!inside(_T("C:\\Windows\\System32\\x.dll"), DIRECTORY),
           "inside: another directory is refused");
  c->check(!inside(_T("D:\\out\\a.txt"), DIRECTORY),
           "inside: the same path on another drive is refused");

  // Traversal: the string looks like a path below the directory until it is
  // canonicalized.
  c->check(!inside(_T("C:\\out\\..\\..\\Windows\\System32\\x.dll"), DIRECTORY),
           "inside: \"..\" out of the directory is refused");
  c->check(!inside(_T("C:\\out\\sub\\..\\..\\a.txt"), DIRECTORY),
           "inside: \"..\" back to the parent is refused");
  c->check(inside(_T("C:\\out\\sub\\..\\a.txt"), DIRECTORY),
           "inside: \"..\" that stays in the directory is accepted");

  // Windows filesystems are case insensitive: a case-only difference must
  // neither defeat the check nor refuse a path that is really inside.
  c->check(inside(_T("c:\\OUT\\SUB\\A.TXT"), DIRECTORY),
           "inside: a case-differing path inside is accepted");
  c->check(inside(_T("C:\\out\\a.txt"), _T("C:\\OUT")),
           "inside: a case-differing directory is accepted");
  c->check(!inside(_T("C:\\OUT\\..\\..\\Windows\\x.dll"), DIRECTORY),
           "inside: a case-differing traversal is still refused");
  c->check(!inside(_T("C:\\OUTSIDE\\a.txt"), DIRECTORY),
           "inside: a case-differing prefix match is still refused");

  // A directory argument that already ends in a separator.
  c->check(inside(_T("C:\\out\\a.txt"), _T("C:\\out\\")),
           "inside: a trailing separator on the directory is tolerated");
  c->check(!inside(_T("C:\\outside\\a.txt"), _T("C:\\out\\")),
           "inside: a trailing separator does not loosen the boundary");
  c->check(!inside(_T("C:\\out"), _T("C:\\out\\")),
           "inside: the directory itself, written with a separator");

  // Forward slashes reach the same file and are canonicalized the same way.
  c->check(inside(_T("C:/out/a.txt"), DIRECTORY),
           "inside: forward slashes are accepted");

  c->check(!inside(_T(""), DIRECTORY), "inside: an empty candidate is refused");
  c->check(!inside(_T("C:\\out\\a.txt"), _T("")),
           "inside: an empty directory is refused");
  c->check(!inside(0, DIRECTORY), "inside: a null candidate is refused");
  c->check(!inside(_T("C:\\out\\a.txt"), 0),
           "inside: a null directory is refused");
}

} // namespace

void runFileNameRulesTests(int *passed, int *failed)
{
  SecurityTest::Counters counters(passed, failed);
#define RUN_TEST(t) (printf("RUN " #t "\n"), t(&counters))
  RUN_TEST(testReservedDeviceNames);
  RUN_TEST(testNameComponentRules);
  RUN_TEST(testCanonicalize);
  RUN_TEST(testPathInsideDirectory);
#undef RUN_TEST
}
