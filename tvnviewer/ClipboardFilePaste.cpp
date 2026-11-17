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
// Added for FarSightVNC on 2026-11-16: clipboard file paste (local viewer
// machine -> remote server machine).
//

#include "ClipboardFilePaste.h"
#include "DesktopWindow.h"

#include "ft-common/FileInfo.h"
#include "thread/AutoLock.h"

#include <map>

namespace {

// A reply of the server, or the end of an upload, may not come at all if the
// connection breaks; the paste then fails with a message instead of hanging
// the staging thread for ever.
const DWORD REPLY_TIMEOUT_MS = 60 * 1000;
// An upload can legitimately take very long (gigabytes over a slow link),
// so it only gives up after a day without any end of operation.
const DWORD UPLOAD_TIMEOUT_MS = 24 * 60 * 60 * 1000;

// Splits an absolute local path into its parent folder and its name.
void splitPath(const StringStorage *path, StringStorage *parent,
               StringStorage *name)
{
  StringStorage value(path->getString());
  size_t separator = value.findLast(_T('\\'));
  if (separator == (size_t)-1) {
    parent->setString(_T(""));
    *name = value;
    return;
  }
  value.getSubstring(parent, 0, separator - 1);
  value.getSubstring(name, separator + 1, value.getLength() - 1);
}

// Absolute Windows path as the file transfer protocol spells it: slashes
// instead of backslashes and a leading slash ("/C:/Users/...").
void toRemotePath(const StringStorage *windowsPath, StringStorage *remotePath)
{
  StringStorage value(windowsPath->getString());
  value.replaceChar(_T('\\'), _T('/'));
  remotePath->setString(_T("/"));
  remotePath->appendString(value.getString());
}

} // namespace

ClipboardFilePaste::ClipboardFilePaste(LogWriter *logWriter,
                                       FileTransferCapability *fileTransfer,
                                       ClipboardFilesCapability *clipboardFiles,
                                       Control *parent,
                                       HWND desktopWnd)
: FileTransferInterface(fileTransfer->getCore()),
  m_logWriter(logWriter),
  m_fileTransfer(fileTransfer),
  m_clipboardFiles(clipboardFiles),
  m_handshake(clipboardFiles, logWriter),
  m_parent(parent),
  m_desktopWnd(desktopWnd),
  m_hasWork(false),
  m_inProgress(false),
  m_cancelled(false),
  m_bytesDone(0),
  m_bytesTotal(0),
  m_bytesOfGroup(0)
{
  resume();
}

ClipboardFilePaste::~ClipboardFilePaste()
{
  terminate();
  wait();
  // The file transfer core must not keep a pointer to this object. Nothing
  // answers its callbacks after this, which is why the session window
  // destroys the paste while the session is being torn down.
  m_fileTransfer->setInterface(0);
}

bool ClipboardFilePaste::isInProgress() const
{
  AutoLock al(&m_stateMutex);
  return m_inProgress;
}

HWND ClipboardFilePaste::getDialogWindow() const
{
  if (m_dialog.get() == 0) {
    return 0;
  }
  return m_dialog->getControl()->getWindow();
}

bool ClipboardFilePaste::isCancelled() const
{
  AutoLock al(&m_stateMutex);
  return m_cancelled;
}

void ClipboardFilePaste::setError(const TCHAR *message)
{
  AutoLock al(&m_stateMutex);
  if (m_error.isEmpty()) {
    m_error.setString(message);
  }
}

bool ClipboardFilePaste::begin(const std::vector<StringStorage> *localPaths)
{
  if (isInProgress() || localPaths->empty()) {
    return false;
  }
  if (localPaths->size() > ClipboardFilesMessage::MAX_PATH_COUNT) {
    StringStorage message;
    message.format(_T("Too many files were copied to paste them into the")
                   _T(" remote session (%d, at most %d)."),
                   (int)localPaths->size(),
                   (int)ClipboardFilesMessage::MAX_PATH_COUNT);
    MessageBox(m_parent->getWindow(), message.getString(),
               _T("Paste Files"), MB_OK | MB_ICONWARNING);
    return false;
  }

  // Two entries of the same name would stage over each other, so the paste
  // is refused rather than pasting one file twice.
  std::map<StringStorage, int> names;
  for (size_t i = 0; i < localPaths->size(); i++) {
    StringStorage parent, name;
    splitPath(&(*localPaths)[i], &parent, &name);
    name.toLowerCase();
    if (names.find(name) != names.end()) {
      MessageBox(m_parent->getWindow(),
                 _T("Two of the copied items have the same name, which")
                 _T(" cannot be pasted into the remote session together."),
                 _T("Paste Files"), MB_OK | MB_ICONWARNING);
      return false;
    }
    names[name] = 1;
  }

  {
    AutoLock al(&m_stateMutex);
    m_localPaths = *localPaths;
    m_cancelled = false;
    m_error.setString(_T(""));
    m_inProgress = true;
  }
  m_stagingRoot.setString(_T(""));
  m_bytesDone = 0;
  m_bytesTotal = 0;
  m_bytesOfGroup = 0;

  m_dialog.reset(new ClipboardPasteDialog(m_parent, this));
  m_dialog->setStatusText(
      ClipboardPasteDialog::statusText(ClipboardPasteDialog::STATUS_MEASURING));
  m_dialog->show();

  // The file transfer core calls back into this object while the upload
  // runs. ViewerWindow keeps the file transfer dialog from taking it over in
  // the meantime.
  m_fileTransfer->setInterface(this);

  m_hasWork = true;
  m_workWaiter.notify();
  return true;
}

void ClipboardFilePaste::onTerminate()
{
  {
    AutoLock al(&m_stateMutex);
    m_cancelled = true;
  }
  m_cancelWaiter.notify();
  m_workWaiter.notify();
}

void ClipboardFilePaste::execute()
{
  while (!isTerminating()) {
    m_workWaiter.waitForEvent();
    if (isTerminating() || !m_hasWork) {
      continue;
    }
    m_hasWork = false;

    bool ok = false;
    try {
      ok = runPaste();
    } catch (const Exception &e) {
      setError(e.getMessage());
    } catch (...) {
      setError(_T("Unknown error while staging the files."));
    }

    // The window thread closes the progress window, tells the user about a
    // failure and releases the keystroke.
    HWND dialogWnd = m_dialog.get() != 0 ? m_dialog->getControl()->getWindow() : 0;
    if (dialogWnd != 0) {
      PostMessage(dialogWnd, ClipboardPasteDialog::WM_PASTE_FINISHED,
                  ok ? 1 : 0, 0);
    } else {
      // No window to answer through (the session is going away): release
      // the state here, without touching any window.
      AutoLock al(&m_stateMutex);
      m_inProgress = false;
    }
  }
}

bool ClipboardFilePaste::runPaste()
{
  UINT64 totalBytes = 0;
  size_t fileCount = 0;
  if (!measureSelection(&totalBytes, &fileCount)) {
    return false;
  }
  if (!confirmLargePaste(totalBytes, fileCount)) {
    return false;
  }
  m_bytesTotal = totalBytes;

  setStatus(ClipboardPasteDialog::STATUS_STAGING_FOLDER);
  if (!requestStagingRoot()) {
    return false;
  }

  setStatus(ClipboardPasteDialog::STATUS_COPYING);
  if (!uploadSelection()) {
    return false;
  }

  setStatus(ClipboardPasteDialog::STATUS_CLIPBOARD);
  return announceStagedFiles();
}

bool ClipboardFilePaste::measureSelection(UINT64 *totalBytes,
                                          size_t *fileCount)
{
  *totalBytes = 0;
  *fileCount = 0;
  for (size_t i = 0; i < m_localPaths.size(); i++) {
    if (!measurePath(&m_localPaths[i], totalBytes, fileCount)) {
      return false;
    }
  }
  if (*fileCount == 0) {
    setError(_T("The copied files could not be read."));
    return false;
  }
  return true;
}

bool ClipboardFilePaste::measurePath(const StringStorage *path,
                                     UINT64 *totalBytes, size_t *fileCount)
{
  if (isCancelled()) {
    return false;
  }

  WIN32_FILE_ATTRIBUTE_DATA attributes;
  if (GetFileAttributesEx(path->getString(), GetFileExInfoStandard,
                          &attributes) == FALSE) {
    StringStorage message;
    message.format(_T("The copied item \"%s\" could not be read."),
                   path->getString());
    setError(message.getString());
    return false;
  }
  if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    ULARGE_INTEGER size;
    size.LowPart = attributes.nFileSizeLow;
    size.HighPart = attributes.nFileSizeHigh;
    *totalBytes += size.QuadPart;
    (*fileCount)++;
    return true;
  }

  StringStorage pattern(path->getString());
  pattern.appendString(_T("\\*"));
  WIN32_FIND_DATA found;
  HANDLE search = FindFirstFile(pattern.getString(), &found);
  if (search == INVALID_HANDLE_VALUE) {
    return true; // an empty or unreadable folder is uploaded as it is
  }
  bool ok = true;
  do {
    if (_tcscmp(found.cFileName, _T(".")) == 0 ||
        _tcscmp(found.cFileName, _T("..")) == 0) {
      continue;
    }
    StringStorage child(path->getString());
    child.appendChar(_T('\\'));
    child.appendString(found.cFileName);
    if (!measurePath(&child, totalBytes, fileCount)) {
      ok = false;
      break;
    }
  } while (FindNextFile(search, &found) != FALSE);
  FindClose(search);
  return ok;
}

bool ClipboardFilePaste::confirmLargePaste(UINT64 totalBytes, size_t fileCount)
{
  if (totalBytes <= CONFIRM_BYTES && fileCount <= CONFIRM_FILES) {
    return true;
  }

  StringStorage message;
  message.format(_T("Pasting these files copies %I64u MB in %d files to the")
                 _T(" remote computer. Continue?"),
                 (totalBytes + 1024 * 1024 - 1) / (1024 * 1024),
                 (int)fileCount);
  // The question runs on this thread: the message pump of the viewer keeps
  // running while it is open.
  int answer = MessageBox(m_desktopWnd, message.getString(),
                          _T("Paste Files"),
                          MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
  if (answer != IDYES) {
    AutoLock al(&m_stateMutex);
    m_cancelled = true; // cancelled by the user: no error message
    return false;
  }
  return true;
}

bool ClipboardFilePaste::requestStagingRoot()
{
  if (isCancelled()) {
    return false;
  }
  StringStorage error;
  bool ok = m_handshake.requestStagingRoot(m_cancelWaiter.getHandle(),
                                           REPLY_TIMEOUT_MS,
                                           &m_stagingRoot, &error);
  if (isCancelled()) {
    return false;
  }
  if (!ok) {
    setError(error.getString());
  }
  return ok;
}

bool ClipboardFilePaste::uploadSelection()
{
  // The file transfer uploads from one source folder at a time, and the
  // copied items may come from several folders, so they are uploaded group
  // by group.
  typedef std::map<StringStorage, std::vector<FileInfo> > GroupMap;
  GroupMap groups;
  for (size_t i = 0; i < m_localPaths.size(); i++) {
    StringStorage parent, name;
    splitPath(&m_localPaths[i], &parent, &name);
    File file(m_localPaths[i].getString());
    groups[parent].push_back(FileInfo(&file));
  }

  StringStorage remoteRoot;
  toRemotePath(&m_stagingRoot, &remoteRoot);

  for (GroupMap::const_iterator group = groups.begin();
       group != groups.end(); ++group) {
    if (isCancelled()) {
      return false;
    }
    m_bytesOfGroup = 0;
    for (size_t i = 0; i < group->second.size(); i++) {
      m_bytesOfGroup += group->second[i].getSize();
    }

    m_fileTransfer->getCore()->uploadOperation(&group->second.front(),
                                               group->second.size(),
                                               group->first.getString(),
                                               remoteRoot.getString());
    if (!waitForStep(&m_uploadWaiter, UPLOAD_TIMEOUT_MS)) {
      if (!isCancelled()) {
        setError(_T("The file copy to the remote computer did not finish."));
      }
      m_fileTransfer->getCore()->terminateCurrentOperation();
      return false;
    }
    {
      AutoLock al(&m_stateMutex);
      if (!m_error.isEmpty()) {
        return false;
      }
    }
    m_bytesDone += m_bytesOfGroup;
  }
  return true;
}

bool ClipboardFilePaste::announceStagedFiles()
{
  if (isCancelled()) {
    return false;
  }
  std::vector<StringStorage> names;
  for (size_t i = 0; i < m_localPaths.size(); i++) {
    StringStorage parent, name;
    splitPath(&m_localPaths[i], &parent, &name);
    names.push_back(name);
  }

  StringStorage error;
  bool ok = m_handshake.announceStagedFiles(&m_stagingRoot, &names,
                                            m_cancelWaiter.getHandle(),
                                            REPLY_TIMEOUT_MS, &error);
  if (isCancelled()) {
    return false;
  }
  if (!ok) {
    setError(error.getString());
  }
  return ok;
}

bool ClipboardFilePaste::waitForStep(WindowsEvent *event, DWORD timeoutMs)
{
  if (isCancelled()) {
    return false;
  }
  HANDLE handles[2] = { event->getHandle(), m_cancelWaiter.getHandle() };
  DWORD result = WaitForMultipleObjects(2, handles, FALSE, timeoutMs);
  return result == WAIT_OBJECT_0 && !isCancelled();
}

void ClipboardFilePaste::reportProgress(double progress)
{
  if (m_dialog.get() == 0) {
    return;
  }
  HWND dialogWnd = m_dialog->getControl()->getWindow();
  if (dialogWnd == 0) {
    return;
  }
  if (progress < 0.0) {
    progress = 0.0;
  } else if (progress > 1.0) {
    progress = 1.0;
  }
  PostMessage(dialogWnd, ClipboardPasteDialog::WM_PASTE_PROGRESS,
              static_cast<WPARAM>(progress * 1000), 0);
}

void ClipboardFilePaste::setStatus(WPARAM status)
{
  m_logWriter->info(_T("Clipboard file paste: %s"),
                    ClipboardPasteDialog::statusText(status));
  HWND dialogWnd = getDialogWindow();
  if (dialogWnd != 0) {
    PostMessage(dialogWnd, ClipboardPasteDialog::WM_PASTE_STATUS, status, 0);
  }
}

//
// File transfer callbacks.
//

int ClipboardFilePaste::onFtTargetFileExists(FileInfo *sourceFileInfo,
                                             FileInfo *targetFileInfo,
                                             const TCHAR *pathToTargetFile)
{
  // The staging folder was created for this paste alone, so anything already
  // there is a leftover of a cancelled attempt and is replaced without
  // asking.
  return CopyFileEventListener::TFE_OVERWRITE;
}

void ClipboardFilePaste::setProgress(double progress)
{
  if (m_bytesTotal == 0) {
    reportProgress(progress);
    return;
  }
  double done = static_cast<double>(m_bytesDone) +
                progress * static_cast<double>(m_bytesOfGroup);
  reportProgress(done / static_cast<double>(m_bytesTotal));
}

void ClipboardFilePaste::onFtOpError(const TCHAR *message)
{
  setError(message);
}

void ClipboardFilePaste::onFtOpInfo(const TCHAR *message)
{
  m_logWriter->info(_T("Clipboard file paste: %s"), message);
}

void ClipboardFilePaste::onFtOpStarted()
{
}

void ClipboardFilePaste::onFtOpFinished(int state, int result)
{
  m_uploadWaiter.notify();
}

void ClipboardFilePaste::setNothingState()
{
}

void ClipboardFilePaste::onRefreshLocalFileList()
{
}

void ClipboardFilePaste::onRefreshRemoteFileList()
{
}

void ClipboardFilePaste::raise(Exception &ex)
{
  setError(ex.getMessage());
  m_uploadWaiter.notify();
}

//
// Progress window callbacks (user interface thread).
//

void ClipboardFilePaste::onPasteCancelled()
{
  {
    AutoLock al(&m_stateMutex);
    if (!m_inProgress) {
      return;
    }
    m_cancelled = true;
  }
  m_cancelWaiter.notify();
}

void ClipboardFilePaste::onPasteFinished(bool ok)
{
  StringStorage error;
  bool cancelled = false;
  {
    AutoLock al(&m_stateMutex);
    error = m_error;
    cancelled = m_cancelled;
    m_inProgress = false;
  }

  // The file transfer core keeps pointing at this object, which lives as
  // long as the session: a late reply of a cancelled upload then still has
  // somewhere to go. The file transfer dialog takes the core over when it is
  // opened, and a paste is refused while it is open.

  if (m_dialog.get() != 0) {
    m_dialog->kill(IDCANCEL);
    m_dialog.reset();
  }

  if (ok) {
    // The remote clipboard now holds the staged files: release the Ctrl+V
    // the user pressed.
    PostMessage(m_desktopWnd, DesktopWindow::WM_USER_FILE_PASTE, 0, 0);
    return;
  }

  if (!cancelled && !error.isEmpty()) {
    // One message, and the paste did not happen at all: the keystroke is
    // not released, so the remote application pastes nothing.
    MessageBox(m_parent->getWindow(), error.getString(),
               _T("Paste Files"), MB_OK | MB_ICONERROR);
  }
  m_logWriter->info(_T("Clipboard file paste did not happen (cancelled = %d)"),
                    (int)cancelled);
}
