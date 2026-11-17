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
// Deterministic tests of the file transfer path rules: the remote peer
// chooses every file name in a listing, so a name must never be able to
// place the written file outside the folder the operator picked, and a
// download must never remove or overwrite a file on its own.
//
// Covers the audit findings D-01, D-03, D-06 and D-07.
//
// The checks that need a filesystem work inside one directory created under
// the Win32 temporary path and delete everything they made before returning;
// nothing is written into the repository or into a real user folder.
//

#include "viewer-security-test/SecurityTestHarness.h"

#include "file-lib/File.h"
#include "file-lib/WinFileChannel.h"
#include "ft-client-lib/FileInfoList.h"
#include "ft-client-lib/FileTransferOperation.h"
#include "ft-common/FileInfo.h"
#include "log-writer/LogWriter.h"
#include "util/WindowsFileName.h"

#include <winioctl.h>

#include <stdio.h>
#include <vector>

namespace {

//----------------------------------------------------------------------------
// The name of a listing entry, as FileTransferReplyBuffer accepts it.

// Returns true when a name arriving from the remote peer is accepted.
bool acceptsRemoteName(const TCHAR *name)
{
  FileInfo fileInfo;
  return fileInfo.setRemoteFileName(name);
}

void testRemoteEntryNames(SecurityTest::Counters *c)
{
  //
  // A legitimate TightVNC server sends a bare entry name, and it filters "."
  // and ".." in its own enumeration, so each of these is a name no correct
  // server ever sends. Every one of them would otherwise reach the local
  // filesystem exactly as written.
  //
  c->check(!acceptsRemoteName(_T("..")),
           "listing: the parent directory is refused as an entry name");
  c->check(!acceptsRemoteName(_T(".")),
           "listing: the current directory is refused as an entry name");
  c->check(!acceptsRemoteName(_T("..\\..\\x")),
           "listing: a relative path that climbs out is refused");
  c->check(!acceptsRemoteName(_T("\\\\server\\share\\x")),
           "listing: a UNC path is refused");
  c->check(!acceptsRemoteName(_T("C:\\Windows\\x")),
           "listing: an absolute path is refused");
  c->check(!acceptsRemoteName(_T("a/b")),
           "listing: a name holding a forward slash is refused");
  c->check(!acceptsRemoteName(_T("NUL")),
           "listing: a reserved device name is refused");
  c->check(!acceptsRemoteName(_T("report.txt.")),
           "listing: a name ending in a dot is refused");
  c->check(!acceptsRemoteName(_T("")),
           "listing: an empty name is refused");
  c->check(!acceptsRemoteName(NULL),
           "listing: a missing name is refused");

  //
  // The ordinary case must keep working: this is what every entry of a real
  // listing looks like.
  //
  c->check(acceptsRemoteName(_T("report.txt")),
           "listing: an ordinary name is accepted");
  c->check(acceptsRemoteName(_T("My Documents")),
           "listing: a name with a space is accepted");
  c->check(acceptsRemoteName(_T("a.b.c")),
           "listing: a name with several dots is accepted");

  //
  // A refused name must leave the entry as it was instead of storing half of
  // the name, because the caller drops the entry and the array slot is reused.
  //
  FileInfo fileInfo;
  fileInfo.setFileName(_T("ok.txt"));
  c->check(!fileInfo.setRemoteFileName(_T("..")),
           "listing: the refusal is reported");
  c->check(_tcscmp(fileInfo.getFileName(), _T("ok.txt")) == 0,
           "listing: a refused name does not change the entry");
}

//----------------------------------------------------------------------------
// getLocalPath: the operator's folder is the boundary of the operation.

// Gives the test access to the protected path helper of a real operation.
class PathProbe : public FileTransferOperation
{
public:
  PathProbe(LogWriter *logWriter) : FileTransferOperation(logWriter) {}

  virtual void start() {}

  bool localPath(FileInfoList *currentFile, const TCHAR *localFolder,
                 StringStorage *out)
  {
    return getLocalPath(currentFile, localFolder, out);
  }
};

const TCHAR *const LOCAL_FOLDER = _T("C:\\Downloads\\FarSightVNC");

void testLocalPath(SecurityTest::Counters *c)
{
  LogWriter logWriter(0);
  PathProbe probe(&logWriter);

  //
  // setFileName is used on purpose: it is the entry point for a name that came
  // from the local filesystem and it does not check anything, so the list below
  // holds exactly what an unchecked remote name used to produce. This is the
  // second line of defence being tested, not the first one.
  //
  {
    FileInfo entry(0, 0, 0, _T("x"));
    entry.setFileName(_T("..\\..\\..\\Users\\victim\\Startup\\x.lnk"));
    FileInfoList list(entry);

    StringStorage path;
    bool ok = probe.localPath(&list, LOCAL_FOLDER, &path);

    c->check(!ok, "getLocalPath: a traversal name is refused");
    c->check(path.getLength() == 0,
             "getLocalPath: a refused path is left empty");
  }

  {
    FileInfo entry(0, 0, 0, _T("report.txt"));
    FileInfoList list(entry);

    StringStorage path;
    bool ok = probe.localPath(&list, LOCAL_FOLDER, &path);

    c->check(ok, "getLocalPath: an ordinary name is accepted");
    c->check(_tcscmp(path.getString(),
                     _T("C:\\Downloads\\FarSightVNC\\report.txt")) == 0,
             "getLocalPath: an ordinary name keeps its path");
  }

  {
    // A folder and a file inside it: the nested path must still be accepted.
    FileInfo folder(0, 0, FileInfo::DIRECTORY, _T("sub"));
    FileInfoList list(folder);

    FileInfo child(0, 0, 0, _T("deep.bin"));
    list.setChild(&child, 1);

    StringStorage path;
    bool ok = probe.localPath(list.getChild(), LOCAL_FOLDER, &path);

    c->check(ok, "getLocalPath: an ordinary nested path is accepted");
    c->check(_tcscmp(path.getString(),
                     _T("C:\\Downloads\\FarSightVNC\\sub\\deep.bin")) == 0,
             "getLocalPath: a nested path keeps its path");
  }

  {
    //
    // An absolute name is a different case, and it is worth being exact about
    // which layer stops it. Appended after the folder it becomes
    // "C:\Downloads\FarSightVNC\C:\Windows\..." , which still sits inside the
    // folder: the drive letter in the middle is just a component Windows reads
    // as a file and an alternate data stream, so the write cannot leave the
    // folder and the containment check has nothing to refuse. The name itself
    // is what must not be accepted, and it is refused where it is parsed, by
    // setRemoteFileName (see testRemoteEntryNames).
    //
    FileInfo entry(0, 0, 0, _T("x"));
    entry.setFileName(_T("C:\\Windows\\System32\\drivers\\etc\\hosts"));
    FileInfoList list(entry);

    StringStorage path;
    bool ok = probe.localPath(&list, LOCAL_FOLDER, &path);

    c->check(!ok || WindowsFileName::isPathInsideDirectory(path.getString(),
                                                           LOCAL_FOLDER),
             "getLocalPath: an absolute name cannot reach outside the folder");
    c->check(!acceptsRemoteName(_T("C:\\Windows\\System32\\drivers\\etc\\hosts")),
             "getLocalPath: an absolute name is refused where it is parsed");
  }
}

void testContainment(SecurityTest::Counters *c)
{
  //
  // The containment check itself, on the shapes getLocalPath can produce: a
  // target that only looks like it is inside the folder must be refused.
  //
  c->check(!WindowsFileName::isPathInsideDirectory(
               _T("C:\\Downloads\\FarSightVNC\\..\\..\\Windows\\x"),
               LOCAL_FOLDER),
           "containment: a target that canonicalizes outside is refused");
  c->check(!WindowsFileName::isPathInsideDirectory(
               _T("C:\\Downloads\\FarSightVNC-other\\x"), LOCAL_FOLDER),
           "containment: a folder with the same prefix is refused");
  c->check(!WindowsFileName::isPathInsideDirectory(LOCAL_FOLDER, LOCAL_FOLDER),
           "containment: the folder itself is not inside itself");
  c->check(WindowsFileName::isPathInsideDirectory(
               _T("C:\\Downloads\\FarSightVNC\\sub\\x"), LOCAL_FOLDER),
           "containment: a nested target is inside");
}

//----------------------------------------------------------------------------
// The filesystem checks: truncate() and the reparse point refusal.

// Creates one directory under the Win32 temporary path. Everything the
// filesystem checks touch lives inside it.
bool makeTempDirectory(StringStorage *path)
{
  TCHAR tempRoot[MAX_PATH + 1];

  DWORD length = GetTempPath(MAX_PATH, &tempRoot[0]);
  if (length == 0 || length > MAX_PATH) {
    return false;
  }

  for (int attempt = 0; attempt < 64; attempt++) {
    StringStorage candidate;
    candidate.format(_T("%sfsv-ft-path-test-%lu-%d"), &tempRoot[0],
                     (unsigned long)GetCurrentProcessId(), attempt);
    if (CreateDirectory(candidate.getString(), NULL) != 0) {
      *path = candidate;
      return true;
    }
  }

  return false;
}

StringStorage childOf(const StringStorage *directory, const TCHAR *name)
{
  StringStorage path(directory->getString());
  path.appendChar(_T('\\'));
  path.appendString(name);
  return path;
}

bool writeFile(const TCHAR *path, const char *content, size_t length)
{
  HANDLE handle = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD written = 0;
  BOOL ok = WriteFile(handle, content, (DWORD)length, &written, NULL);

  CloseHandle(handle);

  return ok != FALSE && written == length;
}

// Reads the identity NTFS gives the file, which survives a truncation in place
// and changes when the file is deleted and created again.
bool fileIdentity(const TCHAR *path, UINT64 *identity)
{
  HANDLE handle = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  BY_HANDLE_FILE_INFORMATION info;
  BOOL ok = GetFileInformationByHandle(handle, &info);

  CloseHandle(handle);

  if (ok == FALSE) {
    return false;
  }

  *identity = ((UINT64)info.nFileIndexHigh << 32) | info.nFileIndexLow;
  return true;
}

// Turns an existing, empty directory into a junction pointing at target. A
// junction needs no privilege, unlike a symbolic link, so this works in a
// plain test run; false means the filesystem refused it and the caller skips.
bool makeJunction(const TCHAR *directory, const TCHAR *target)
{
  StringStorage substituteName(_T("\\??\\"));
  substituteName.appendString(target);

  const size_t substituteBytes = substituteName.getLength() * sizeof(TCHAR);
  const size_t printBytes = _tcslen(target) * sizeof(TCHAR);

  // The REPARSE_DATA_BUFFER of a mount point, laid out by hand: the tag, the
  // length of everything after the 8 byte header, then the two names.
  const size_t HEADER_BYTES = 8;
  const size_t NAMES_HEADER_BYTES = 8;
  const size_t pathBufferBytes = substituteBytes + sizeof(TCHAR) +
                                 printBytes + sizeof(TCHAR);
  const size_t bufferBytes = HEADER_BYTES + NAMES_HEADER_BYTES +
                             pathBufferBytes;

  std::vector<char> buffer(bufferBytes, 0);
  char *raw = &buffer.front();

  *reinterpret_cast<DWORD *>(raw) = IO_REPARSE_TAG_MOUNT_POINT;
  *reinterpret_cast<WORD *>(raw + 4) =
      (WORD)(NAMES_HEADER_BYTES + pathBufferBytes);
  *reinterpret_cast<WORD *>(raw + 6) = 0;
  *reinterpret_cast<WORD *>(raw + 8) = 0;
  *reinterpret_cast<WORD *>(raw + 10) = (WORD)substituteBytes;
  *reinterpret_cast<WORD *>(raw + 12) = (WORD)(substituteBytes + sizeof(TCHAR));
  *reinterpret_cast<WORD *>(raw + 14) = (WORD)printBytes;

  char *pathBuffer = raw + HEADER_BYTES + NAMES_HEADER_BYTES;
  memcpy(pathBuffer, substituteName.getString(), substituteBytes);
  memcpy(pathBuffer + substituteBytes + sizeof(TCHAR), target, printBytes);

  HANDLE handle = CreateFile(directory, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS |
                             FILE_FLAG_OPEN_REPARSE_POINT,
                             NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD returned = 0;
  BOOL ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, raw,
                            (DWORD)bufferBytes, NULL, 0, &returned, NULL);

  CloseHandle(handle);

  return ok != FALSE;
}

void testTruncateAndReparsePoints(SecurityTest::Counters *c)
{
  StringStorage temp;

  if (!makeTempDirectory(&temp)) {
    c->fail("filesystem: a temporary directory could not be created");
    return ;
  }

  StringStorage filePath = childOf(&temp, _T("target.bin"));
  StringStorage plainDirPath = childOf(&temp, _T("plain"));
  StringStorage junctionPath = childOf(&temp, _T("link"));

  bool junctionMade = false;

  if (!writeFile(filePath.getString(), "0123456789", 10)) {
    c->fail("filesystem: the target file could not be written");
  } else {
    UINT64 before = 0;
    UINT64 after = 0;

    bool haveBefore = fileIdentity(filePath.getString(), &before);

    File target(filePath.getString());

    c->check(target.truncate(), "truncate: an existing file is emptied");
    c->check(target.exists(), "truncate: the file still exists afterwards");
    c->check(target.length() == 0, "truncate: the file is empty afterwards");

    bool haveAfter = fileIdentity(filePath.getString(), &after);

    //
    // The identity proves it: deleting the file and creating a new one with
    // the same name gives the new file a different index. Truncating in place
    // keeps it, so nothing was destroyed before it was known to be replaceable.
    //
    c->check(haveBefore && haveAfter && before == after,
             "truncate: the file is emptied in place, not deleted and recreated");

    c->check(!target.isReparsePoint(),
             "reparse: an ordinary file is not a reparse point");
  }

  {
    // A file that does not exist yet is created, which is what a first
    // download of a name needs.
    StringStorage freshPath = childOf(&temp, _T("fresh.bin"));
    File fresh(freshPath.getString());

    c->check(fresh.truncate(), "truncate: a missing file is created");
    c->check(fresh.exists() && fresh.length() == 0,
             "truncate: the created file is empty");

    DeleteFile(freshPath.getString());
  }

  if (CreateDirectory(plainDirPath.getString(), NULL) == 0) {
    c->fail("filesystem: a plain directory could not be created");
  } else {
    File plain(plainDirPath.getString());

    c->check(!plain.isReparsePoint(),
             "reparse: an ordinary directory is not a reparse point");

    //
    // A directory is not a file to empty: truncating it used to delete it.
    //
    c->check(!plain.truncate(), "truncate: a directory is refused");
    c->check(plain.exists(), "truncate: a refused directory is still there");
  }

  if (CreateDirectory(junctionPath.getString(), NULL) == 0) {
    c->fail("filesystem: the junction directory could not be created");
  } else if (!makeJunction(junctionPath.getString(),
                           plainDirPath.getString())) {
    //
    // Not a failure of the product: the filesystem of this run does not let
    // the test plant a reparse point. Nothing is claimed about the case.
    //
    printf("SKIP: reparse: a junction could not be created on this filesystem\n");
    RemoveDirectory(junctionPath.getString());
  } else {
    junctionMade = true;

    File junction(junctionPath.getString());

    c->check(junction.isReparsePoint(),
             "reparse: a junction is recognised as a reparse point");
    c->check(!junction.truncate(),
             "truncate: a reparse point is refused instead of followed");

    //
    // The file the junction points at must be untouched: following the link
    // is what would have emptied or replaced something outside the target.
    //
    File plain(plainDirPath.getString());
    c->check(plain.exists() && plain.isDirectory(),
             "reparse: the directory behind the junction is untouched");
  }

  //
  // The handle the download writes the file data through must not traverse a
  // reparse point either: checking the path before the open leaves a window in
  // which a link can be planted. DownloadOperation opens that handle through
  // WinFileChannel with refuseReparsePoint set.
  //
  {
    StringStorage targetPath = childOf(&temp, _T("handle-target.txt"));
    StringStorage linkPath = childOf(&temp, _T("handle-link.txt"));

    if (!writeFile(targetPath.getString(), "secret", 6)) {
      c->fail("filesystem: the write-handle target could not be created");
    } else {
      //
      // An ordinary file must still open with the flag set, or every download
      // would break. This is the compatibility half of the check.
      //
      bool openedPlainFile = true;
      try {
        WinFileChannel channel(targetPath.getString(), F_WRITE, FM_OPEN,
                               true, true);
      } catch (Exception &) {
        openedPlainFile = false;
      }
      c->check(openedPlainFile,
               "reparse: an ordinary file still opens for writing");

      //
      // A file symbolic link needs SeCreateSymbolicLinkPrivilege or Developer
      // Mode, so it cannot be planted in every environment. Where it can, the
      // open has to be refused rather than redirected to the target.
      //
      // Resolved at run time because winhdr.h still pins _WIN32_WINNT to
      // 0x0501, where this function is not declared (audit finding F-06).
      typedef BOOLEAN (WINAPI *CreateSymbolicLinkFn)(LPCTSTR, LPCTSTR, DWORD);
      CreateSymbolicLinkFn createSymbolicLink =
        (CreateSymbolicLinkFn)GetProcAddress(GetModuleHandle(_T("kernel32.dll")),
#ifdef UNICODE
                                             "CreateSymbolicLinkW");
#else
                                             "CreateSymbolicLinkA");
#endif

      if (createSymbolicLink == 0) {
        printf("SKIP: reparse: CreateSymbolicLink is not available\n");
      } else if (createSymbolicLink(linkPath.getString(),
                                    targetPath.getString(), 0) == 0) {
        printf("SKIP: reparse: a file symbolic link could not be created "
               "(needs Developer Mode or SeCreateSymbolicLinkPrivilege)\n");
      } else {
        bool refused = false;
        try {
          WinFileChannel channel(linkPath.getString(), F_WRITE, FM_OPEN,
                                 true, true);
        } catch (Exception &) {
          refused = true;
        }
        c->check(refused,
                 "reparse: a symbolic link is refused for writing");

        //
        // What the link pointed at must be untouched: following it is what
        // would have written outside the folder the operator chose.
        //
        File target(targetPath.getString());
        c->check(target.exists() && target.length() == 6,
                 "reparse: the file behind the link is untouched");

        DeleteFile(linkPath.getString());
      }

      DeleteFile(targetPath.getString());
    }
  }

  //
  // Cleanup: everything that was created, and the temporary directory itself.
  // RemoveDirectory on a junction removes the junction, not its target.
  //
  if (junctionMade) {
    RemoveDirectory(junctionPath.getString());
  }
  RemoveDirectory(plainDirPath.getString());
  DeleteFile(filePath.getString());
  RemoveDirectory(temp.getString());

  File leftOver(temp.getString());
  c->check(!leftOver.exists(),
           "filesystem: the temporary directory is removed again");
}

} // namespace

void runFileTransferPathTests(int *passed, int *failed)
{
  SecurityTest::Counters counters(passed, failed);

  testRemoteEntryNames(&counters);
  testLocalPath(&counters);
  testContainment(&counters);
  testTruncateAndReparsePoints(&counters);
}
