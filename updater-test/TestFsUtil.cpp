// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"
#include "TestKeys.h"

#include "updater-lib/FsUtil.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <string>

using namespace fsv::update;
using fsv::test::TempDir;

namespace {

bool isProcessElevated() {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  TOKEN_ELEVATION elevation{};
  DWORD returned = 0;
  const bool ok = ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation),
                                        &returned) != FALSE;
  ::CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
}

bool hasAttribute(const std::wstring &path, DWORD attribute) {
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & attribute) != 0;
}

std::wstring lower(std::wstring text) {
  ::CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
  return text;
}

std::wstring upper(std::wstring text) {
  ::CharUpperBuffW(text.data(), static_cast<DWORD>(text.size()));
  return text;
}

bool isLowerHex16(const std::wstring &text) {
  return text.size() == 16 && std::all_of(text.begin(), text.end(), [](wchar_t c) {
           return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
         });
}

void testConversions() {
  struct Sample {
    const char *utf8;
    const wchar_t *wide;
  };
  const Sample samples[] = {
      {"plain ascii", L"plain ascii"},
      {"caf\xC3\xA9", L"caf\u00E9"},
      {"\xE6\x97\xA5\xE6\x9C\xAC", L"\u65E5\u672C"},
      {"\xF0\x9F\x98\x80", L"\xD83D\xDE00"},
      {"a\xC3\xA9\xE6\x97\xA5\xF0\x9F\x98\x80z", L"a\u00E9\u65E5\xD83D\xDE00z"},
  };
  for (const Sample &sample : samples) {
    CHECK(utf8ToWide(sample.utf8) == sample.wide);
    CHECK(wideToUtf8(sample.wide) == sample.utf8);
    CHECK(wideToUtf8(utf8ToWide(sample.utf8)) == sample.utf8);
  }
  CHECK(utf8ToWide("").empty());
  CHECK(wideToUtf8(L"").empty());
  CHECK(utf8ToWide("\xC3").empty());            // truncated sequence is rejected
  CHECK(utf8ToWide("\xFF\xFE").empty());        // invalid bytes are rejected
  CHECK(wideToUtf8(L"\xD83D").empty());         // lone surrogate is rejected
}

void testPathStrings() {
  CHECK(joinPath(L"C:\\a", L"b") == L"C:\\a\\b");
  CHECK(joinPath(L"C:\\a\\", L"b") == L"C:\\a\\b");
  CHECK(joinPath(L"C:\\a/", L"b") == L"C:\\a\\b");
  CHECK(joinPath(L"C:\\a", L"\\b") == L"C:\\a\\b");
  CHECK(joinPath(L"C:\\", L"b") == L"C:\\b");
  CHECK(joinPath(L"", L"b") == L"b");
  CHECK(joinPath(L"C:\\a", L"") == L"C:\\a");
  CHECK(joinPath(L"a", L"b\\c.txt") == L"a\\b\\c.txt");

  CHECK(parentDirectory(L"C:\\a\\b\\c.txt") == L"C:\\a\\b");
  CHECK(parentDirectory(L"C:\\a\\b\\") == L"C:\\a");
  CHECK(parentDirectory(L"C:\\a") == L"C:\\");
  CHECK(parentDirectory(L"C:\\") == L"");
  CHECK(parentDirectory(L"rel\\x") == L"rel");
  CHECK(parentDirectory(L"x") == L"");
  CHECK(parentDirectory(L"") == L"");

  CHECK(fileName(L"C:\\a\\b\\c.txt") == L"c.txt");
  CHECK(fileName(L"C:\\a\\b\\") == L"b");
  CHECK(fileName(L"c.txt") == L"c.txt");
  CHECK(fileName(L"C:\\a/b.exe") == L"b.exe");
  CHECK(fileName(L"") == L"");

  CHECK(fullPath(L"C:\\a\\..\\b\\.\\c") == L"C:\\b\\c");
  CHECK(fullPath(L"").empty());
  const std::wstring relative = fullPath(L"relative.txt");
  CHECK(relative.size() > 3 && relative[1] == L':');
  CHECK(fileName(relative) == L"relative.txt");

  CHECK(pathsEqual(L"C:\\Windows", L"c:\\WINDOWS"));
  CHECK(pathsEqual(L"C:\\Windows\\", L"c:\\windows"));
  CHECK(pathsEqual(L"C:\\a\\b\\..\\c", L"C:\\a\\c"));
  CHECK(!pathsEqual(L"C:\\a\\c", L"C:\\a\\cc"));
  CHECK(!pathsEqual(L"C:\\a\\c", L"C:\\a"));
}

void testFiles() {
  TempDir temp;
  CHECK(!temp.path().empty());
  CHECK(directoryExists(temp.path()));
  CHECK(!fileExists(temp.path()));

  // ensureDirectory creates intermediate directories and is idempotent.
  const std::wstring nested = temp.child(L"a\\b\\c\\d");
  CHECK(!directoryExists(nested));
  CHECK(ensureDirectory(nested));
  CHECK(directoryExists(nested));
  CHECK(ensureDirectory(nested));
  CHECK(ensureDirectory(nested + L"\\"));
  CHECK(!ensureDirectory(L""));
  const std::wstring blocker = temp.child(L"blocker");
  CHECK(writeFileAtomic(blocker, "x"));
  CHECK(!ensureDirectory(blocker));                 // a file is in the way
  CHECK(!ensureDirectory(blocker + L"\\child"));

  // writeFileAtomic / readFile.
  const std::wstring file = temp.child(L"a\\data.txt");
  CHECK(writeFileAtomic(file, "hello"));
  CHECK(fileExists(file));
  CHECK(readFile(file, 1024) == std::optional<std::string>("hello"));
  CHECK(writeFileAtomic(file, "replaced content"));   // replaces an existing file
  CHECK(readFile(file, 1024) == std::optional<std::string>("replaced content"));
  CHECK(writeFileAtomic(file, ""));
  CHECK(readFile(file, 0) == std::optional<std::string>(""));

  const std::string binary("\0\x01\xFF\r\n\0", 6);
  CHECK(writeFileAtomic(file, binary));
  CHECK(readFile(file, 6) == std::optional<std::string>(binary));
  CHECK(!readFile(file, 5).has_value());              // maxBytes is enforced
  CHECK(!readFile(file, 0).has_value());
  CHECK(!readFile(temp.child(L"missing.txt"), 100).has_value());
  CHECK(!readFile(temp.path(), 100).has_value());      // a directory is not a file
  CHECK(!writeFileAtomic(temp.child(L"nodir\\x.txt"), "x"));

  // No temporary files are left behind.
  WIN32_FIND_DATAW data{};
  HANDLE find = ::FindFirstFileW(temp.child(L"a\\*").c_str(), &data);
  int entries = 0;
  if (find != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = data.cFileName;
      if (name != L"." && name != L"..") {
        ++entries;
        CHECK(name == L"b" || name == L"data.txt");
      }
    } while (::FindNextFileW(find, &data));
    ::FindClose(find);
  }
  CHECK(entries == 2);

  // Non-ASCII names.
  const std::wstring unicode = temp.child(L"caf\u00E9-\u65E5\u672C-\xD83D\xDE00.txt");
  CHECK(writeFileAtomic(unicode, "unicode"));
  CHECK(readFile(unicode, 100) == std::optional<std::string>("unicode"));

  const auto space = freeSpaceBytes(temp.path());
  CHECK(space.has_value());
  CHECK(space && *space > 0);
  CHECK(!freeSpaceBytes(temp.child(L"no\\such\\dir")).has_value());

  CHECK(!currentModulePath().empty());
  CHECK(fileExists(currentModulePath()));
  CHECK(directoryExists(localAppDataDirectory()));
}

void testRemoveTree() {
  TempDir temp;
  TempDir outside;
  CHECK(!temp.path().empty());
  CHECK(!outside.path().empty());

  // Nested tree with a read-only file and a read-only directory.
  const std::wstring tree = temp.child(L"tree");
  CHECK(ensureDirectory(tree + L"\\x\\y\\z"));
  CHECK(writeFileAtomic(tree + L"\\top.txt", "1"));
  CHECK(writeFileAtomic(tree + L"\\x\\y\\deep.txt", "2"));
  CHECK(writeFileAtomic(tree + L"\\x\\y\\z\\ro.txt", "3"));
  CHECK(::SetFileAttributesW((tree + L"\\x\\y\\z\\ro.txt").c_str(), FILE_ATTRIBUTE_READONLY) != 0);
  CHECK(::SetFileAttributesW((tree + L"\\x\\y\\z").c_str(),
                             FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY) != 0);
  CHECK(removeTree(tree, temp.path()));
  CHECK(!directoryExists(tree));
  CHECK(directoryExists(temp.path()));

  // A missing path inside the guard is already "removed".
  CHECK(removeTree(temp.child(L"gone\\er"), temp.path()));

  // Refuses a path outside the guard, and leaves it intact.
  const std::wstring victim = outside.child(L"victim");
  CHECK(writeFileAtomic(victim, "keep"));
  CHECK(!removeTree(victim, temp.path()));
  CHECK(!removeTree(outside.path(), temp.path()));
  CHECK(fileExists(victim));

  // Refuses path == mustBeUnder (with and without a trailing separator, any case).
  CHECK(!removeTree(temp.path(), temp.path()));
  CHECK(!removeTree(temp.path() + L"\\", temp.path()));
  CHECK(!removeTree(upper(temp.path()), lower(temp.path())));
  CHECK(directoryExists(temp.path()));

  // Refuses a parent of the guard and the empty strings.
  CHECK(!removeTree(parentDirectory(temp.path()), temp.path()));
  CHECK(!removeTree(L"", temp.path()));
  CHECK(!removeTree(temp.child(L"x"), L""));

  // A sibling that merely shares the textual prefix is outside the guard.
  const std::wstring guard = temp.child(L"guard");
  const std::wstring sibling = temp.child(L"guard2");
  CHECK(ensureDirectory(guard));
  CHECK(ensureDirectory(sibling + L"\\inner"));
  CHECK(!removeTree(sibling, guard));
  CHECK(!removeTree(sibling + L"\\inner", guard));
  CHECK(directoryExists(sibling + L"\\inner"));

  // ".." cannot escape the guard.
  CHECK(!removeTree(guard + L"\\..\\guard2", guard));
  CHECK(!removeTree(guard + L"\\..\\..\\" + fileName(temp.path()), guard));
  CHECK(directoryExists(sibling));
  CHECK(directoryExists(temp.path()));

  // The guard is compared case-insensitively.
  CHECK(ensureDirectory(guard + L"\\Mixed\\Case"));
  CHECK(removeTree(upper(guard) + L"\\mixed", lower(guard)));
  CHECK(!directoryExists(guard + L"\\Mixed"));
  CHECK(directoryExists(guard));

  // A whole drive is never an acceptable guard.
  CHECK(!removeTree(L"C:\\Windows", L"C:\\"));

  // Junctions are removed themselves and never followed.
  const std::wstring target = outside.child(L"junction-target");
  CHECK(ensureDirectory(target));
  CHECK(writeFileAtomic(target + L"\\precious.txt", "precious"));
  const std::wstring link = guard + L"\\link";
  const std::wstring command = L"mklink /J \"" + link + L"\" \"" + target + L"\" >nul 2>nul";
  if (_wsystem(command.c_str()) == 0 && hasAttribute(link, FILE_ATTRIBUTE_REPARSE_POINT)) {
    CHECK(!removeTree(link + L"\\precious.txt", guard)); // would leave the guarded tree
    CHECK(fileExists(target + L"\\precious.txt"));
    CHECK(removeTree(link, guard));
    CHECK(!directoryExists(link));
    CHECK(fileExists(target + L"\\precious.txt"));
    CHECK(directoryExists(target));

    // A tree that contains a junction deletes the link, not its contents.
    CHECK(ensureDirectory(guard + L"\\holder"));
    const std::wstring innerLink = guard + L"\\holder\\link2";
    const std::wstring command2 =
        L"mklink /J \"" + innerLink + L"\" \"" + target + L"\" >nul 2>nul";
    if (_wsystem(command2.c_str()) == 0) {
      CHECK(removeTree(guard + L"\\holder", guard));
      CHECK(!directoryExists(guard + L"\\holder"));
      CHECK(fileExists(target + L"\\precious.txt"));
    } else {
      fsv::test::note("second junction could not be created; nested-junction check skipped");
    }
  } else {
    fsv::test::note("mklink /J unavailable; junction checks skipped");
  }
}

void testWritability() {
  TempDir temp;
  CHECK(canWriteDirectory(temp.path()));
  CHECK(canWriteDirectory(temp.path() + L"\\"));
  CHECK(!canWriteDirectory(temp.child(L"missing")));
  const std::wstring file = temp.child(L"plain.txt");
  CHECK(writeFileAtomic(file, "x"));
  CHECK(!canWriteDirectory(file));

  // The probe leaves nothing behind.
  WIN32_FIND_DATAW data{};
  HANDLE find = ::FindFirstFileW(temp.child(L"*").c_str(), &data);
  int entries = 0;
  if (find != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = data.cFileName;
      if (name != L"." && name != L"..") {
        ++entries;
      }
    } while (::FindNextFileW(find, &data));
    ::FindClose(find);
  }
  CHECK(entries == 1);

  // canReplaceFile: existing file, missing file in a writable dir, in a missing dir.
  CHECK(canReplaceFile(file));
  CHECK(readFile(file, 10) == std::optional<std::string>("x")); // not modified
  CHECK(canReplaceFile(temp.child(L"not-yet.txt")));
  CHECK(!fileExists(temp.child(L"not-yet.txt")));              // not created either
  CHECK(!canReplaceFile(temp.child(L"nodir\\file.txt")));

  // A running executable cannot be replaced.
  CHECK(!canReplaceFile(currentModulePath()));

  // A read-only file cannot be opened for write.
  const std::wstring readOnly = temp.child(L"readonly.txt");
  CHECK(writeFileAtomic(readOnly, "x"));
  CHECK(::SetFileAttributesW(readOnly.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
  CHECK(!canReplaceFile(readOnly));
  ::SetFileAttributesW(readOnly.c_str(), FILE_ATTRIBUTE_NORMAL);

  // Protected system locations (only meaningful for a non-elevated token).
  wchar_t systemDirectory[MAX_PATH] = {};
  const UINT length = ::GetSystemDirectoryW(systemDirectory, MAX_PATH);
  if (isProcessElevated()) {
    fsv::test::note("process is elevated; System32 write checks skipped");
  } else if (length == 0 || length >= MAX_PATH) {
    fsv::test::note("GetSystemDirectoryW failed; System32 write checks skipped");
  } else {
    CHECK(!canWriteDirectory(systemDirectory));
    CHECK(!canReplaceFile(joinPath(systemDirectory, L"kernel32.dll")));
    CHECK(!canReplaceFile(joinPath(systemDirectory, L"fsv-no-such-file.dll")));
  }
}

void testUpdateStateDirectory() {
  const std::wstring root = L"C:\\Portable\\FarSightVNC";
  const std::wstring state = updateStateDirectory(root);
  CHECK(!state.empty());
  CHECK(updateStateDirectory(root) == state);                          // stable
  CHECK(updateStateDirectory(L"c:\\PORTABLE\\farsightvnc") == state);   // case-insensitive
  CHECK(updateStateDirectory(root + L"\\") == state);                   // trailing separator
  CHECK(updateStateDirectory(L"C:\\Portable\\x\\..\\FarSightVNC") == state);
  CHECK(updateStateDirectory(L"C:\\Portable\\FarSightVNC2") != state);
  CHECK(updateStateDirectory(L"D:\\Portable\\FarSightVNC") != state);

  // %LOCALAPPDATA%\FarSightVNC\Updates\<16 hex>
  const std::wstring expectedParent = joinPath(joinPath(localAppDataDirectory(), L"FarSightVNC"), L"Updates");
  CHECK(pathsEqual(parentDirectory(state), expectedParent));
  CHECK(isLowerHex16(fileName(state)));
  CHECK(updateStateDirectory(L"").empty());

  // The hash is SHA-256 of the lower-cased UTF-8 full path (first 16 hex characters),
  // so a re-implementation (for example in a script) can be checked against it.
  CHECK(fileName(updateStateDirectory(L"C:\\a")) != fileName(updateStateDirectory(L"C:\\b")));
}

void testExecutableVersion() {
  CHECK(!readExecutableVersion(L"C:\\no\\such\\file.exe").has_value());
  TempDir temp;
  const std::wstring notExe = temp.child(L"not-an-exe.exe");
  CHECK(writeFileAtomic(notExe, "this is not a PE file"));
  CHECK(!readExecutableVersion(notExe).has_value());

  const std::wstring updater =
      joinPath(parentDirectory(currentModulePath()), L"fsvncupdater.exe");
  if (!fileExists(updater)) {
    fsv::test::note("fsvncupdater.exe not found next to updater-test.exe; "
                    "readExecutableVersion on a real binary skipped (build farsight-updater first)");
    return;
  }
  const auto version = readExecutableVersion(updater);
  CHECK(version.has_value());
  if (version) {
    std::printf("  fsvncupdater.exe ProductVersion: %s\n", version->toString().c_str());
    CHECK(*version >= Version{1, 0, 0, Channel::Alpha});
  }
}

} // namespace

void runFsUtilTests() {
  fsv::test::beginSuite("FsUtil: conversions and path strings");
  testConversions();
  testPathStrings();
  fsv::test::beginSuite("FsUtil: files");
  testFiles();
  testRemoveTree();
  testWritability();
  fsv::test::beginSuite("FsUtil: state directory and version resource");
  testUpdateStateDirectory();
  testExecutableVersion();
}
