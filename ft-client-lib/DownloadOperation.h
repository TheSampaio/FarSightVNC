// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef _DOWNLOAD_OPERATION_H_
#define _DOWNLOAD_OPERATION_H_

#include "ft-common/FileInfo.h"
#include "file-lib/WinFileChannel.h"
#include "FileInfoList.h"
#include "CopyOperation.h"

//
// File transfer operation class for downloading files (and file trees).
//

class DownloadOperation : public CopyOperation
{
public:

  //
  // Parameters:
  //
  // [IN] filesToDownload - array of files info to be downloaded
  // [IN] filesCount - count elements in filesToDownload array
  // [IN] pathToTargetRoot - full path to local folder where downloaded files
  // will be located (system depended).
  // [IN] pathToSourceRoot - full path to remote folder where files to download
  // is located (system independed, see ft protocol info)
  //

  DownloadOperation(LogWriter *logWriter,
                    const FileInfo *filesToDownload, size_t filesCount,
                    const TCHAR *pathToTargetRoot,
                    const TCHAR *pathToSourceRoot);

  virtual ~DownloadOperation();

  //
  // Inherited from FileTransferOperation
  //

  virtual void start();

  //
  // Inherited from FileTransferOperation
  //

  virtual bool acceptsReply(UINT32 rfbMessage);

protected:

  //
  // What this operation is waiting for. The server may send any reply at any
  // moment, so every handler below is only allowed to run in the state the
  // matching request left behind (see acceptsReply).
  //

  enum DownloadState
  {
    // Nothing is outstanding: no reply is expected.
    DS_IDLE,
    // Folder size requests were sent and are still being answered.
    DS_CALC_SIZE,
    // A file list request was sent.
    DS_FILE_LIST,
    // A download request was sent; the file is not open yet.
    DS_DOWNLOAD_START,
    // A download data request was sent; the local file is open.
    DS_DOWNLOAD_DATA,
    // The operation is over and accepts nothing further.
    DS_FINISHED
  };

  //
  // Inherited from FileTransferEventHandler
  //

  virtual void onFileListReply(DataInputStream *input);
  virtual void onDownloadReply(DataInputStream *input);
  virtual void onDownloadDataReply(DataInputStream *input);
  virtual void onDownloadEndReply(DataInputStream *input);
  virtual void onLastRequestFailedReply(DataInputStream *input);
  virtual void onDirSizeReply(DataInputStream *input);

private:

  // Starts download of current file (m_toCopy member)
  void startDownload();

  // Starts download of file
  void processFile();

  // Start download of folder
  void processFolder();

  // Sets m_toCopy member to next file to download
  void gotoNext();

  // Sends get folder size request to server to know
  // how many bytes must be receivied during
  // all download process
  void tryCalcInputFilesSize();

  // Terminates operation execution
  void killOp();

  // Helper method that decrements m_foldersToCalcSizeLeft count
  // and if it's equal to null, start first file download
  void decFoldersToCalcSizeCount();

  // Helper method that creates message string and notifies listeners
  void notifyFailedToDownload(const TCHAR *errorDescription);

  // Sets current m_toCopy member value and updates
  // m_pathToSourceFile, m_pathToTargetFile members
  void changeFileToDownload(FileInfoList *toDownload);

  // Helper method that notifies listeners that the local target cannot be
  // used and continues with the next file
  void refuseLocalTarget(const TCHAR *reason);

protected:
  // Target local file
  File *m_file;
  // File output stream associated with m_file
  WinFileChannel *m_fos;

  // Initial file offset for current download (broken downloads)
  UINT64 m_fileOffset;

  // Helper member to know how many folders to download left
  // to get their file size
  UINT32 m_foldersToCalcSizeLeft;

  // request data size changes dynamicaly depends on request rate
  size_t m_bufferSize;
  DateTime m_lastRequestTime;

  // False when m_pathToTargetFile left the folder the operator picked, in
  // which case nothing of the current file may be written
  bool m_isTargetPathSafe;

  // What this operation is waiting for
  DownloadState m_state;

};

#endif
