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

#ifndef _CLIPBOARD_FILE_PASTE_H_
#define _CLIPBOARD_FILE_PASTE_H_

#include "ClipboardPasteDialog.h"

#include "ft-client-lib/FileTransferInterface.h"
#include "log-writer/LogWriter.h"
#include "thread/LocalMutex.h"
#include "thread/Thread.h"
#include "viewer-core/ClipboardFilesCapability.h"
#include "viewer-core/ClipboardFilesHandshake.h"
#include "viewer-core/FileTransferCapability.h"
#include "win-system/WindowsEvent.h"

#include <memory>
#include <vector>

// One clipboard file paste: the user pressed Ctrl+C on files in Explorer on
// this machine and Ctrl+V inside the remote session.
//
// The copy itself transfers nothing (the selection can be gigabytes). This
// class runs the work the swallowed Ctrl+V starts, on a thread of its own so
// that the message pump of the viewer keeps running:
//
//   1. measures the selection and asks the user when it is large;
//   2. asks the server for a staging folder (CLIPBOARD_FILES_BEGIN);
//   3. uploads the selection into it with the ordinary file transfer;
//   4. asks the server to put a CF_HDROP of the staged files on the remote
//      clipboard (CLIPBOARD_FILES_SET);
//   5. only then lets the Ctrl+V reach the remote application, by asking the
//      desktop window to synthesize it.
//
// The thread lives as long as this object and waits for work, like
// ClipboardExchange does on the server side.
class ClipboardFilePaste : public Thread,
                           public FileTransferInterface,
                           public ClipboardPasteDialogListener
{
public:
  // Above these the user is asked first. A paste of a few files is immediate;
  // a 5 GB folder is a long upload the user did not necessarily mean to
  // start, so it needs a yes.
  static const UINT64 CONFIRM_BYTES = 512ULL * 1024 * 1024;
  static const size_t CONFIRM_FILES = 2000;

  // 'parent' owns the progress window, 'desktopWnd' is the remote desktop
  // window that synthesizes the Ctrl+V once the remote clipboard is set.
  ClipboardFilePaste(LogWriter *logWriter,
                     FileTransferCapability *fileTransfer,
                     ClipboardFilesCapability *clipboardFiles,
                     Control *parent,
                     HWND desktopWnd);
  virtual ~ClipboardFilePaste();

  // Starts a paste of the absolute local paths taken from the clipboard.
  // Called on the user interface thread. Returns false when nothing was
  // started, and then the caller must let the keystroke through unchanged.
  bool begin(const std::vector<StringStorage> *localPaths);

  // True while a paste is running. Thread-safe.
  bool isInProgress() const;

  // Window of the progress dialog, or 0 when no paste is running. The
  // caller registers it as a modeless dialog of the application, like the
  // file transfer dialog is registered.
  HWND getDialogWindow() const;

  //
  // Inherited from ClipboardPasteDialogListener (user interface thread).
  //
  virtual void onPasteCancelled();
  virtual void onPasteFinished(bool ok);

  //
  // Inherited from FileTransferInterface. The file transfer calls these from
  // the remote viewer core thread while an upload runs.
  //
  virtual int onFtTargetFileExists(FileInfo *sourceFileInfo,
                                   FileInfo *targetFileInfo,
                                   const TCHAR *pathToTargetFile);
  virtual void setProgress(double progress);
  virtual void onFtOpError(const TCHAR *message);
  virtual void onFtOpInfo(const TCHAR *message);
  virtual void onFtOpStarted();
  virtual void onFtOpFinished(int state, int result);
  virtual void setNothingState();
  virtual void onRefreshLocalFileList();
  virtual void onRefreshRemoteFileList();
  virtual void raise(Exception &ex);

protected:
  // Inherited from Thread.
  virtual void execute();
  virtual void onTerminate();

private:
  // The whole sequence, on the staging thread. Returns false when the paste
  // did not happen; the reason is in m_error, which stays empty when the
  // user cancelled.
  bool runPaste();

  bool measureSelection(UINT64 *totalBytes, size_t *fileCount);
  // Adds the size of one file, or of a whole folder tree, to the totals.
  bool measurePath(const StringStorage *path, UINT64 *totalBytes,
                   size_t *fileCount);
  bool confirmLargePaste(UINT64 totalBytes, size_t fileCount);
  bool requestStagingRoot();
  bool uploadSelection();
  bool announceStagedFiles();

  // Waits for the end of an upload or for a cancel. Returns false when it
  // was cancelled or the wait timed out.
  bool waitForStep(WindowsEvent *event, DWORD timeoutMs);

  void setError(const TCHAR *message);
  bool isCancelled() const;
  void reportProgress(double progress);
  // Tells the progress window which step is running; the window thread sets
  // the text.
  void setStatus(WPARAM status);

  LogWriter *m_logWriter;
  FileTransferCapability *m_fileTransfer;
  ClipboardFilesCapability *m_clipboardFiles;
  // The staging folder and file list exchanges with the server.
  ClipboardFilesHandshake m_handshake;
  Control *m_parent;
  HWND m_desktopWnd;

  std::unique_ptr<ClipboardPasteDialog> m_dialog;

  // Work handed to the staging thread.
  std::vector<StringStorage> m_localPaths;
  WindowsEvent m_workWaiter;
  bool m_hasWork;

  // End of an upload and cancel of the paste.
  WindowsEvent m_uploadWaiter;
  WindowsEvent m_cancelWaiter;

  StringStorage m_stagingRoot;

  mutable LocalMutex m_stateMutex;
  bool m_inProgress;
  bool m_cancelled;
  StringStorage m_error;

  // Progress of the upload: bytes of the groups already uploaded and bytes
  // of the whole selection, so that the bar covers the paste and not one
  // upload operation.
  UINT64 m_bytesDone;
  UINT64 m_bytesTotal;
  UINT64 m_bytesOfGroup;
};

#endif // _CLIPBOARD_FILE_PASTE_H_
