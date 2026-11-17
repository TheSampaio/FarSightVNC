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

// Deterministic test of the clipboard file paste (local viewer machine ->
// remote server machine). No network, no clipboard and no second machine are
// involved: the protocol messages are written to an in-memory stream and read
// back, the CF_HDROP memory block is inspected byte by byte, and the path
// rules and the "does this Ctrl+V become a file paste?" decision are pure
// functions.

#include "rfb/ClipboardFilesMessage.h"
#include "viewer-core/ClipboardFilesCapability.h"
#include "win-system/DropFilesBuffer.h"

#include "io-lib/DataInputStream.h"
#include "io-lib/DataOutputStream.h"
#include "io-lib/InputStream.h"
#include "io-lib/OutputStream.h"
#include "util/Exception.h"

#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool ok, const char *what)
{
  if (ok) {
    g_passed++;
  } else {
    g_failed++;
    printf("FAIL: %s\n", what);
  }
}

//----------------------------------------------------------------------------
// In-memory streams.

class MemoryOutputStream : public OutputStream
{
public:
  virtual size_t write(const void *buffer, size_t len)
  {
    m_data.append(static_cast<const char *>(buffer), len);
    return len;
  }

  const std::string &data() const { return m_data; }

private:
  std::string m_data;
};

class MemoryInputStream : public InputStream
{
public:
  MemoryInputStream(const std::string &data) : m_data(data), m_position(0) {}

  virtual size_t read(void *buffer, size_t len)
  {
    size_t left = m_data.size() - m_position;
    if (left == 0) {
      throw Exception(_T("memory stream: no data left"));
    }
    size_t count = len < left ? len : left;
    memcpy(buffer, m_data.data() + m_position, count);
    m_position += count;
    return count;
  }

  virtual size_t available() { return m_data.size() - m_position; }

private:
  std::string m_data;
  size_t m_position;
};

const TCHAR *const GOOD_ROOT =
    _T("C:\\Users\\tester\\AppData\\Local\\Temp\\FarSightVNC\\Clipboard\\a1b2c3");

ClipboardFilesMessage::CheckResult checkOne(const TCHAR *root,
                                            const TCHAR *path)
{
  StringStorage rootStorage(root);
  std::vector<StringStorage> paths;
  paths.push_back(StringStorage(path));
  return ClipboardFilesMessage::checkPaths(&rootStorage, &paths);
}

StringStorage underRoot(const TCHAR *name)
{
  StringStorage path(GOOD_ROOT);
  path.appendChar(_T('\\'));
  path.appendString(name);
  return path;
}

//----------------------------------------------------------------------------
// CLIPBOARD_FILES_SET and CLIPBOARD_FILES_REPLY bodies.

void testSetBodyRoundTrip()
{
  ClipboardFilesMessage sent;
  sent.setRoot(GOOD_ROOT);
  sent.addPath(underRoot(_T("report.txt")).getString());
  sent.addPath(underRoot(_T("Pictures")).getString());
  // A non-ASCII name must survive the UTF-8 encoding of the wire format.
  sent.addPath(underRoot(_T("rela\u00e7\u00e3o.pdf")).getString());

  MemoryOutputStream memory;
  DataOutputStream output(&memory);
  sent.writeSetBody(&output);

  MemoryInputStream source(memory.data());
  DataInputStream input(&source);
  ClipboardFilesMessage received;
  received.readSetBody(&input);

  check(received.getRoot()->isEqualTo(GOOD_ROOT), "set body: root survives");
  check(received.getPaths()->size() == 3, "set body: all paths survive");
  if (received.getPaths()->size() == 3) {
    check((*received.getPaths())[0].isEqualTo(underRoot(_T("report.txt")).getString()),
          "set body: first path survives");
    check((*received.getPaths())[2].isEqualTo(underRoot(_T("rela\u00e7\u00e3o.pdf")).getString()),
          "set body: non-ASCII path survives");
  } else {
    g_failed += 2;
  }
  check(received.check() == ClipboardFilesMessage::CHECK_OK,
        "set body: the parsed list passes the rules");
  check(source.available() == 0, "set body: the whole body is consumed");
}

void testSetBodyEmptyList()
{
  ClipboardFilesMessage sent;
  sent.setRoot(GOOD_ROOT);

  MemoryOutputStream memory;
  DataOutputStream output(&memory);
  sent.writeSetBody(&output);

  MemoryInputStream source(memory.data());
  DataInputStream input(&source);
  ClipboardFilesMessage received;
  received.readSetBody(&input);
  check(received.getPaths()->empty(), "set body: an empty list parses");
  check(received.check() == ClipboardFilesMessage::CHECK_EMPTY,
        "set body: an empty list is refused");
}

void testSetBodyOverCapIsRejectedWhileReading()
{
  // A hostile peer announces more paths than the protocol accepts: the
  // reader must refuse before allocating anything.
  MemoryOutputStream memory;
  DataOutputStream output(&memory);
  output.writeUTF8(GOOD_ROOT);
  output.writeUInt32(static_cast<UINT32>(ClipboardFilesMessage::MAX_PATH_COUNT) + 1);

  MemoryInputStream source(memory.data());
  DataInputStream input(&source);
  ClipboardFilesMessage received;
  bool threw = false;
  try {
    received.readSetBody(&input);
  } catch (const Exception &) {
    threw = true;
  }
  check(threw, "set body: an over-cap path count is refused while reading");
}

void testReplyBodyRoundTrip()
{
  MemoryOutputStream memory;
  DataOutputStream output(&memory);
  ClipboardFilesMessage::writeReplyBody(&output,
                                        ClipboardFilesMessage::REPLY_ROOT,
                                        ClipboardFilesMessage::STATUS_OK,
                                        GOOD_ROOT);
  ClipboardFilesMessage::writeReplyBody(&output,
                                        ClipboardFilesMessage::REPLY_SET,
                                        ClipboardFilesMessage::STATUS_FAILED,
                                        _T("a path is outside the staging folder"));

  MemoryInputStream source(memory.data());
  DataInputStream input(&source);

  UINT8 kind = 0;
  UINT8 status = 0;
  StringStorage text;
  ClipboardFilesMessage::readReplyBody(&input, &kind, &status, &text);
  check(kind == ClipboardFilesMessage::REPLY_ROOT, "reply body: first kind");
  check(status == ClipboardFilesMessage::STATUS_OK, "reply body: first status");
  check(text.isEqualTo(GOOD_ROOT), "reply body: the root survives");

  ClipboardFilesMessage::readReplyBody(&input, &kind, &status, &text);
  check(kind == ClipboardFilesMessage::REPLY_SET, "reply body: second kind");
  check(status == ClipboardFilesMessage::STATUS_FAILED, "reply body: second status");
  check(text.isEqualTo(_T("a path is outside the staging folder")),
        "reply body: the error text survives");
  check(source.available() == 0, "reply body: both bodies are consumed");
}

//----------------------------------------------------------------------------
// The staging root and the path rules.

void testStagingRoot()
{
  StringStorage root;

  root.setString(GOOD_ROOT);
  check(ClipboardFilesMessage::isStagingRoot(&root), "root: the staging layout");

  root.setString(_T("c:\\temp\\farsightvnc\\clipboard\\DEAD_beef-01"));
  check(ClipboardFilesMessage::isStagingRoot(&root),
        "root: case and '-' and '_' in the unique name");

  root.setString(_T("C:\\Temp\\FarSightVNC\\Clipboard"));
  check(!ClipboardFilesMessage::isStagingRoot(&root),
        "root: without a unique name");

  root.setString(_T("C:\\Temp\\Other\\Clipboard\\a1"));
  check(!ClipboardFilesMessage::isStagingRoot(&root),
        "root: another product folder");

  root.setString(_T("C:\\Temp\\FarSightVNC\\Clipboard\\a1\\"));
  check(!ClipboardFilesMessage::isStagingRoot(&root),
        "root: a trailing separator");

  root.setString(_T("C:\\Temp\\..\\FarSightVNC\\Clipboard\\a1"));
  check(!ClipboardFilesMessage::isStagingRoot(&root),
        "root: a \"..\" component");

  root.setString(_T("\\\\server\\share\\FarSightVNC\\Clipboard\\a1"));
  check(!ClipboardFilesMessage::isStagingRoot(&root), "root: a UNC path");

  root.setString(_T("\\\\?\\C:\\Temp\\FarSightVNC\\Clipboard\\a1"));
  check(!ClipboardFilesMessage::isStagingRoot(&root), "root: a device path");

  root.setString(_T("Temp\\FarSightVNC\\Clipboard\\a1"));
  check(!ClipboardFilesMessage::isStagingRoot(&root), "root: a relative path");

  root.setString(_T("C:/Temp/FarSightVNC/Clipboard/a1"));
  check(!ClipboardFilesMessage::isStagingRoot(&root), "root: forward slashes");

  root.setString(_T("C:\\Temp\\FarSightVNC\\Clipboard\\a 1"));
  check(!ClipboardFilesMessage::isStagingRoot(&root),
        "root: a space in the unique name");

  StringStorage longToken(_T("C:\\Temp\\FarSightVNC\\Clipboard\\"));
  for (size_t i = 0; i <= ClipboardFilesMessage::MAX_TOKEN_LENGTH; i++) {
    longToken.appendChar(_T('a'));
  }
  check(!ClipboardFilesMessage::isStagingRoot(&longToken),
        "root: an over-long unique name");

  root.setString(_T(""));
  check(!ClipboardFilesMessage::isStagingRoot(&root), "root: empty");
}

void testPathRules()
{
  check(checkOne(GOOD_ROOT, underRoot(_T("report.txt")).getString()) ==
            ClipboardFilesMessage::CHECK_OK,
        "paths: one plain name under the root");

  check(checkOne(_T("C:\\Temp\\NotFarSight\\x\\a1"),
                 _T("C:\\Temp\\NotFarSight\\x\\a1\\report.txt")) ==
            ClipboardFilesMessage::CHECK_BAD_ROOT,
        "paths: a root that is not a staging folder");

  check(checkOne(GOOD_ROOT, _T("C:\\Windows\\System32\\drivers\\etc\\hosts")) ==
            ClipboardFilesMessage::CHECK_OUTSIDE_ROOT,
        "paths: an absolute path outside the root");

  check(checkOne(GOOD_ROOT, _T("report.txt")) ==
            ClipboardFilesMessage::CHECK_OUTSIDE_ROOT,
        "paths: a relative path");

  // The root of another client: the prefix does not match.
  check(checkOne(GOOD_ROOT,
                 _T("C:\\Users\\tester\\AppData\\Local\\Temp\\FarSightVNC")
                 _T("\\Clipboard\\other\\report.txt")) ==
            ClipboardFilesMessage::CHECK_OUTSIDE_ROOT,
        "paths: the staging folder of another paste");

  check(checkOne(GOOD_ROOT, underRoot(_T("..\\..\\evil.exe")).getString()) ==
            ClipboardFilesMessage::CHECK_TRAVERSAL,
        "paths: \"..\" after a matching prefix");

  check(checkOne(GOOD_ROOT, underRoot(_T("..")).getString()) ==
            ClipboardFilesMessage::CHECK_TRAVERSAL,
        "paths: the parent folder itself");

  check(checkOne(GOOD_ROOT, underRoot(_T("sub\\report.txt")).getString()) ==
            ClipboardFilesMessage::CHECK_BAD_NAME,
        "paths: a nested path instead of one name");

  check(checkOne(GOOD_ROOT, underRoot(_T(".")).getString()) ==
            ClipboardFilesMessage::CHECK_BAD_NAME,
        "paths: the staging folder itself");

  check(checkOne(GOOD_ROOT, underRoot(_T("C:report.txt")).getString()) ==
            ClipboardFilesMessage::CHECK_BAD_NAME,
        "paths: a drive letter in the name");

  check(checkOne(GOOD_ROOT, underRoot(_T("report.txt ")).getString()) ==
            ClipboardFilesMessage::CHECK_BAD_NAME,
        "paths: a trailing space in the name");

  check(checkOne(GOOD_ROOT, underRoot(_T("report*.txt")).getString()) ==
            ClipboardFilesMessage::CHECK_BAD_NAME,
        "paths: a wildcard in the name");

  check(checkOne(GOOD_ROOT, GOOD_ROOT) ==
            ClipboardFilesMessage::CHECK_OUTSIDE_ROOT,
        "paths: the root without a name");

  // The root prefix is compared like Windows compares paths.
  StringStorage upperRoot(GOOD_ROOT);
  upperRoot.toUpperCase();
  StringStorage mixedRoot(GOOD_ROOT);
  std::vector<StringStorage> mixedPaths;
  mixedPaths.push_back(StringStorage(upperRoot.getString()));
  mixedPaths.back().appendString(_T("\\report.txt"));
  check(ClipboardFilesMessage::checkPaths(&mixedRoot, &mixedPaths) ==
            ClipboardFilesMessage::CHECK_OK,
        "paths: the root prefix is compared case insensitively");
}

void testPathCaps()
{
  StringStorage root(GOOD_ROOT);

  std::vector<StringStorage> many;
  for (size_t i = 0; i < ClipboardFilesMessage::MAX_PATH_COUNT + 1; i++) {
    StringStorage name;
    name.format(_T("file%d.bin"), (int)i);
    many.push_back(underRoot(name.getString()));
  }
  check(ClipboardFilesMessage::checkPaths(&root, &many) ==
            ClipboardFilesMessage::CHECK_TOO_MANY,
        "caps: more paths than MAX_PATH_COUNT");

  many.resize(ClipboardFilesMessage::MAX_PATH_COUNT);
  check(ClipboardFilesMessage::checkPaths(&root, &many) ==
            ClipboardFilesMessage::CHECK_OK,
        "caps: exactly MAX_PATH_COUNT paths");

  StringStorage longName;
  for (size_t i = 0; i < ClipboardFilesMessage::MAX_PATH_LENGTH + 1; i++) {
    longName.appendChar(_T('a'));
  }
  std::vector<StringStorage> longPath;
  longPath.push_back(underRoot(longName.getString()));
  check(ClipboardFilesMessage::checkPaths(&root, &longPath) ==
            ClipboardFilesMessage::CHECK_TOO_LONG,
        "caps: a path longer than MAX_PATH_LENGTH");

  // Many paths that are each short enough but together exceed the total.
  StringStorage bigName;
  for (size_t i = 0; i < 4000; i++) {
    bigName.appendChar(_T('b'));
  }
  std::vector<StringStorage> bulk;
  for (size_t i = 0; i < ClipboardFilesMessage::MAX_PATH_COUNT; i++) {
    bulk.push_back(underRoot(bigName.getString()));
  }
  check(ClipboardFilesMessage::checkPaths(&root, &bulk) ==
            ClipboardFilesMessage::CHECK_TOO_LONG,
        "caps: the whole list longer than MAX_TOTAL_LENGTH");
}

//----------------------------------------------------------------------------
// The CF_HDROP memory block.

void testDropFilesLayout()
{
  std::vector<StringStorage> paths;
  paths.push_back(StringStorage(_T("C:\\a\\one.txt")));
  paths.push_back(StringStorage(_T("C:\\a\\two")));

  std::vector<char> buffer;
  DropFilesBuffer::build(&paths, &buffer);

  size_t expected = sizeof(DROPFILES) +
                    (_tcslen(_T("C:\\a\\one.txt")) + 1 +
                     _tcslen(_T("C:\\a\\two")) + 1 + 1) * sizeof(WCHAR);
  check(buffer.size() == expected, "CF_HDROP: exact buffer size");

  const DROPFILES *header = reinterpret_cast<const DROPFILES *>(&buffer.front());
  check(header->pFiles == sizeof(DROPFILES), "CF_HDROP: name list offset");
  check(header->fWide == TRUE, "CF_HDROP: fWide is set");
  check(header->fNC == FALSE, "CF_HDROP: fNC is clear");
  check(header->pt.x == 0 && header->pt.y == 0, "CF_HDROP: drop point is zero");

  const WCHAR *names =
      reinterpret_cast<const WCHAR *>(&buffer.front() + sizeof(DROPFILES));
  check(wcscmp(names, L"C:\\a\\one.txt") == 0, "CF_HDROP: first name");
  const WCHAR *second = names + wcslen(names) + 1;
  check(wcscmp(second, L"C:\\a\\two") == 0, "CF_HDROP: second name");
  const WCHAR *end = second + wcslen(second) + 1;
  check(*end == L'\0', "CF_HDROP: the list is double-null terminated");

  std::vector<StringStorage> parsed;
  check(DropFilesBuffer::parse(&buffer.front(), buffer.size(), &parsed),
        "CF_HDROP: the block parses");
  check(parsed.size() == 2, "CF_HDROP: both names come back");
  if (parsed.size() == 2) {
    check(parsed[0].isEqualTo(_T("C:\\a\\one.txt")), "CF_HDROP: first name back");
    check(parsed[1].isEqualTo(_T("C:\\a\\two")), "CF_HDROP: second name back");
  } else {
    g_failed += 2;
  }
}

void testDropFilesEmptyAndBroken()
{
  std::vector<StringStorage> none;
  std::vector<char> buffer;
  DropFilesBuffer::build(&none, &buffer);
  check(buffer.size() == sizeof(DROPFILES) + sizeof(WCHAR),
        "CF_HDROP: an empty list is only the terminator");

  std::vector<StringStorage> parsed;
  check(DropFilesBuffer::parse(&buffer.front(), buffer.size(), &parsed),
        "CF_HDROP: an empty list parses");
  check(parsed.empty(), "CF_HDROP: an empty list yields no name");

  check(!DropFilesBuffer::parse(0, 0, &parsed), "CF_HDROP: a null block fails");
  check(!DropFilesBuffer::parse(&buffer.front(), sizeof(DROPFILES) - 1, &parsed),
        "CF_HDROP: a block smaller than the header fails");

  // A name list that is not terminated must be refused rather than read past
  // the end of the block.
  std::vector<StringStorage> one;
  one.push_back(StringStorage(_T("C:\\a\\one.txt")));
  DropFilesBuffer::build(&one, &buffer);
  check(!DropFilesBuffer::parse(&buffer.front(), buffer.size() - 2 * sizeof(WCHAR),
                                &parsed),
        "CF_HDROP: an unterminated name list fails");
  check(parsed.empty(), "CF_HDROP: a refused block yields no name");

  // An ANSI list is not produced by this product and is not accepted.
  DropFilesBuffer::build(&one, &buffer);
  reinterpret_cast<DROPFILES *>(&buffer.front())->fWide = FALSE;
  check(!DropFilesBuffer::parse(&buffer.front(), buffer.size(), &parsed),
        "CF_HDROP: an ANSI name list fails");
}

//----------------------------------------------------------------------------
// "Does this Ctrl+V become a file paste?"

ClipboardFilesCapability::PasteContext readyContext()
{
  ClipboardFilesCapability::PasteContext context;
  context.capabilityEnabled = true;
  context.fileTransferEnabled = true;
  context.viewOnly = false;
  context.connected = true;
  context.clipboardEnabled = true;
  context.clipboardHasFiles = true;
  context.pasteInProgress = false;
  context.fileTransferBusy = false;
  return context;
}

void testPasteDecision()
{
  check(ClipboardFilesCapability::shouldInterceptPaste(readyContext()),
        "decision: everything in place -> the paste is started");

  ClipboardFilesCapability::PasteContext context = readyContext();
  context.capabilityEnabled = false;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: a stock TightVNC server -> the keystroke falls through");

  context = readyContext();
  context.fileTransferEnabled = false;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: file transfer disabled -> the keystroke falls through");

  context = readyContext();
  context.viewOnly = true;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: view-only -> the keystroke falls through");

  context = readyContext();
  context.connected = false;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: not connected -> the keystroke falls through");

  context = readyContext();
  context.clipboardHasFiles = false;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: no files on the clipboard -> the keystroke falls through");

  context = readyContext();
  context.clipboardEnabled = false;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: clipboard sharing off -> the keystroke falls through");

  context = readyContext();
  context.pasteInProgress = true;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: a paste already running -> the keystroke falls through");

  context = readyContext();
  context.fileTransferBusy = true;
  check(!ClipboardFilesCapability::shouldInterceptPaste(context),
        "decision: the file transfer dialog is busy -> the keystroke falls through");

  // The default context is "nothing available", which must never intercept.
  ClipboardFilesCapability::PasteContext empty;
  check(!ClipboardFilesCapability::shouldInterceptPaste(empty),
        "decision: a default context never intercepts");
}

} // namespace

int main()
{
  setvbuf(stdout, NULL, _IONBF, 0);
#define RUN_TEST(t) (printf("RUN " #t "\n"), t())
  RUN_TEST(testSetBodyRoundTrip);
  RUN_TEST(testSetBodyEmptyList);
  RUN_TEST(testSetBodyOverCapIsRejectedWhileReading);
  RUN_TEST(testReplyBodyRoundTrip);
  RUN_TEST(testStagingRoot);
  RUN_TEST(testPathRules);
  RUN_TEST(testPathCaps);
  RUN_TEST(testDropFilesLayout);
  RUN_TEST(testDropFilesEmptyAndBroken);
  RUN_TEST(testPasteDecision);
#undef RUN_TEST
  printf("viewer-clipboard-files-test: %d passed, %d failed\n",
         g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
