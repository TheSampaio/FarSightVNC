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

#include "DownloadOperation.h"

#include "ft-common/FTMessage.h"

DownloadOperation::DownloadOperation(LogWriter *logWriter,
                                     const FileInfo *filesToDownload,
                                     size_t filesCount,
                                     const TCHAR *pathToTargetRoot,
                                     const TCHAR *pathToSourceRoot)
: CopyOperation(logWriter),
  m_file(0),
  m_fos(0),
  m_fileOffset(0),
  m_bufferSize(20000),
  m_isTargetPathSafe(false),
  m_state(DS_IDLE)
{
  m_pathToSourceRoot.setString(pathToSourceRoot);
  m_pathToTargetRoot.setString(pathToTargetRoot);

  m_toCopy = new FileInfoList(filesToDownload, filesCount);

  changeFileToDownload(m_toCopy);
}

DownloadOperation::~DownloadOperation()
{
  if (m_toCopy != NULL) {
    delete m_toCopy->getRoot();
  }
  if (m_fos != NULL) {
    try { m_fos->close(); } catch (IOException) { }
    delete m_fos;
  }
  if (m_file != NULL) {
    delete m_file;
  }
}

void DownloadOperation::start()
{
  m_foldersToCalcSizeLeft = 0;

  m_totalBytesToCopy = 0;
  m_totalBytesCopied = 0;

  // Notify listeners that operation have started
  notifyStart();

  //
  // Try to calculate input files size.
  //
  // Then this state will be finished, we can
  // start files download.
  //
  // See decFoldersToCalcSizeCount, onDirSizeReply, onLastRequestFailed
  // methods.
  //

  tryCalcInputFilesSize();

  if (m_foldersToCalcSizeLeft == 0) {
    startDownload();
  }
}

bool DownloadOperation::acceptsReply(UINT32 rfbMessage)
{
  if (m_state == DS_FINISHED) {
    return false;
  }

  //
  // A failure reply answers whatever is outstanding, so it is accepted
  // whenever something is.
  //
  if (rfbMessage == FTMessage::LAST_REQUEST_FAILED_REPLY) {
    return m_state != DS_IDLE;
  }

  switch (rfbMessage) {
  case FTMessage::DIRSIZE_REPLY:
    return m_state == DS_CALC_SIZE;
  case FTMessage::FILE_LIST_REPLY:
    return m_state == DS_FILE_LIST;
  case FTMessage::DOWNLOAD_START_REPLY:
    return m_state == DS_DOWNLOAD_START;
  case FTMessage::DOWNLOAD_DATA_REPLY:
  case FTMessage::DOWNLOAD_END_REPLY:
    //
    // The server answers a download data request either with data or, at the
    // end of the file, with the end reply, so both belong to this one state.
    //
    return m_state == DS_DOWNLOAD_DATA;
  default:
    break;
  }

  //
  // Any other code is not a reply this operation handles at all: leave it to
  // FileTransferEventAdapter, which has always refused those.
  //
  return true;
}

void DownloadOperation::onFileListReply(DataInputStream *input)
{
  m_state = DS_IDLE;

  m_toCopy->setChild(m_replyBuffer->getFilesInfo(),
                     m_replyBuffer->getFilesInfoCount());
  gotoNext();
}

void DownloadOperation::onDownloadReply(DataInputStream *input)
{
  //
  // Cleanup
  //

  if (m_fos != NULL) {
    try { m_fos->close(); } catch (...) { }
    delete m_fos;
    m_fos = NULL;
  }
  if (m_file != NULL) {
    delete m_file;
    m_file = NULL;
  }

  m_state = DS_IDLE;

  //
  // The path was checked when it was built, but the file is created here, so
  // the check is repeated at the moment of the write: nothing is created or
  // truncated under a path that left the operator's folder.
  //

  if (!m_isTargetPathSafe) {
    refuseLocalTarget(_T("the name the server sent would place the file ")
                      _T("outside the chosen local folder"));
    return ;
  }

  //
  // Try to create file on local file system and open it for writting
  //

  m_file = new File(m_pathToTargetFile.getString());

  //
  // A symbolic link or a junction planted at the target path sends the write
  // to whatever it points at, which is a path the operator never chose, so an
  // existing reparse point at the target is a refusal and never something to
  // follow. Only the target of the download is tested; an ordinary folder that
  // happens to contain a junction still lists and still downloads.
  //

  if (m_file->isReparsePoint()) {
    delete m_file;
    m_file = NULL;
    refuseLocalTarget(_T("the local target is a symbolic link or a junction"));
    return ;
  }

  if (m_fileOffset == 0) {
    //
    // The result must be checked: a truncate that failed leaves a file whose
    // old contents are still there, and writing the download into it from
    // offset zero would mix the two.
    //
    if (!m_file->truncate()) {
      delete m_file;
      m_file = NULL;
      refuseLocalTarget(_T("the local file cannot be emptied"));
      return ;
    }
  }

  try {
    StringStorage path;
    m_file->getPath(&path);
    // The last leg of the traversal defence: the remote peer chose the name
    // this path was built from, so the handle the file data is written through
    // must not traverse a reparse point either. Checking the path before this
    // open is not enough on its own, because a link can be planted in between.
    m_fos = new WinFileChannel(path.getString(), F_WRITE, FM_OPEN, true, true);
    // Seek to initial file position to continue writting
    m_fos->seek((INT64)m_fileOffset);
    m_totalBytesCopied += m_fileOffset;
  } catch (Exception &ioEx) {
    notifyFailedToDownload(ioEx.getMessage());
    gotoNext();
    return;
  }

  //
  // Send first request for file data
  //

  bool compression = m_replyBuffer->isCompressionSupported();

  m_sender->sendDownloadDataRequest((UINT32)m_bufferSize,
                                    compression);
  m_state = DS_DOWNLOAD_DATA;
  m_lastRequestTime = DateTime::now();
}

void DownloadOperation::onDownloadDataReply(DataInputStream *input)
{
  if (isTerminating()) {
    gotoNext();
    return ;
  }

  //
  // The state machine already keeps this handler from running before the file
  // is open; the null check is the second line of defence, because a null
  // stream here would be a write through a null pointer.
  //
  if (m_fos == NULL) {
    m_logWriter->warning(_T("Ignored download data: no local file is open\n"));
    return ;
  }

  try {
    DataOutputStream dos(m_fos);
    if (!m_replyBuffer->getDownloadBuffer().empty()) {
      dos.writeFully(&m_replyBuffer->getDownloadBuffer().front(),
                     m_replyBuffer->getDownloadBufferSize());
    }
  } catch (IOException &ioEx) {
    notifyFailedToDownload(ioEx.getMessage());
    gotoNext();
    return ;
  }

  //
  // Notify that we receive some data
  //

  m_totalBytesCopied += m_replyBuffer->getDownloadBufferSize();

  if (m_copyListener != NULL) {
    m_copyListener->dataChunkCopied(m_totalBytesCopied, m_totalBytesToCopy);
  }

  //
  // Send next download data request
  //

  bool compression = m_replyBuffer->isCompressionSupported();
  
  if ((DateTime::now() - m_lastRequestTime).getTime() > 300) {
    m_bufferSize /= 2; 
  } else {
    m_bufferSize *= 2;
  }
  if (m_bufferSize < 20000) {
    m_bufferSize = 20000;
  }
  if (m_bufferSize > 400000) {
    m_bufferSize = 400000;
  }
  m_sender->sendDownloadDataRequest((UINT32)m_bufferSize, compression);
  m_state = DS_DOWNLOAD_DATA;
  m_lastRequestTime = DateTime::now();
}

void DownloadOperation::onDownloadEndReply(DataInputStream *input)
{
  m_state = DS_IDLE;

  //
  // Cleanup
  //
  // Both members start out null and stay null whenever no file was opened, so
  // neither may be used without a check: an end reply that arrives before any
  // download started used to close a null stream.
  //

  if (m_fos != NULL) {
    try { m_fos->close(); } catch (...) { }
    delete m_fos;
    m_fos = NULL;
  }

  if (m_file != NULL) {
    try {
      m_file->setLastModified(m_replyBuffer->getDownloadLastModified());
    } catch (...) {
      notifyFailedToDownload(_T("Cannot set modification time"));
    }

    delete m_file;
    m_file = NULL;
  }

  gotoNext();
}

void DownloadOperation::onLastRequestFailedReply(DataInputStream *input)
{
  //
  // This LRF message received from get folder size request
  // we do need to download next file
  //

  if (m_foldersToCalcSizeLeft > 0) {
    decFoldersToCalcSizeCount();
  } else {
    m_state = DS_IDLE;

    // Logging
    StringStorage message;

    m_replyBuffer->getLastErrorMessage(&message);

    notifyFailedToDownload(message.getString());

    // Download next file
    gotoNext();
  }
}

void DownloadOperation::onDirSizeReply(DataInputStream *input)
{
  m_totalBytesToCopy += m_replyBuffer->getDirSize();
  decFoldersToCalcSizeCount();
}

void DownloadOperation::startDownload()
{
  if (isTerminating()) {
    killOp();
    return ;
  } // if terminating

  //
  // Every download of the current file goes through here, so this is the one
  // gate where the local target has to be proven to be inside the folder the
  // operator picked. A refused name costs its own entry and nothing else: the
  // rest of the selection still downloads.
  //

  if (!m_isTargetPathSafe) {
    refuseLocalTarget(_T("the name the server sent would place the file ")
                      _T("outside the chosen local folder"));
    return ;
  }

  FileInfo *fileInfo = m_toCopy->getFileInfo();

  // Logging
  if (m_toCopy->getFirst()->getParent() == NULL) {
    StringStorage message;

    message.format(_T("Downloading '%s' %s"), m_pathToTargetFile.getString(),
                   fileInfo->isDirectory() ? _T("folder") : _T("file"));

    notifyInformation(message.getString());
  } // logging

  if (fileInfo->isDirectory()) {
    processFolder();
  } else {
    processFile();
  } // if not directory

  if (isTerminating()) {
    killOp();
    return ;
  } // if terminating
}

void DownloadOperation::processFile()
{
  m_fileOffset = 0;

  File targetFile(m_pathToTargetFile.getString());

  if (targetFile.exists()) {
    FileInfo *sourceFileInfo = m_toCopy->getFileInfo();
    FileInfo targetFileInfo(&targetFile);

    //
    // Copy listener must decide what to do with this situation
    //

    int action = m_copyListener->targetFileExists(sourceFileInfo,
                                                  &targetFileInfo,
                                                  m_pathToTargetFile.getString());
    switch (action) {
    case CopyFileEventListener::TFE_OVERWRITE:
      break;
    case CopyFileEventListener::TFE_SKIP:
      m_totalBytesCopied += sourceFileInfo->getSize();
      gotoNext();
      return ;
    case CopyFileEventListener::TFE_APPEND:
      m_fileOffset = targetFileInfo.getSize();
      break;
    case CopyFileEventListener::TFE_CANCEL:
      if (!isTerminating()) {
        terminate();
      } // if not terminating
      return ;
    default:
      _ASSERT(FALSE);
    } // switch
  } // if target file exists

  // Send request that we want to download file
  m_sender->sendDownloadRequest(m_pathToSourceFile.getString(), m_fileOffset);
  m_state = DS_DOWNLOAD_START;
}

void DownloadOperation::processFolder()
{
  File local(m_pathToTargetFile.getString());

  //
  // A junction or a directory symbolic link at the target would place the
  // whole downloaded subtree wherever it points, so it is refused instead of
  // being followed. Only the target of this download is tested.
  //

  if (local.isReparsePoint()) {
    refuseLocalTarget(_T("the local target is a symbolic link or a junction"));
    return ;
  }

  if (local.exists() && local.isDirectory()) {
  } else {
    if (!local.mkdir()) {
      // Logging
      StringStorage message;

      message.format(_T("Error: failed to create local folder '%s'"),
                     m_pathToTargetFile.getString());

      notifyError(message.getString());

      // Download next file
      gotoNext();
      return ;
    }
  }

  m_sender->sendFileListRequest(m_pathToSourceFile.getString(),
                                m_replyBuffer->isCompressionSupported());
  m_state = DS_FILE_LIST;
}

void DownloadOperation::gotoNext()
{
  FileInfoList *current = m_toCopy;

  bool hasChild = current->getChild() != NULL;
  bool hasNext = current->getNext() != NULL;
  bool hasParent = current->getFirst()->getParent() != NULL;

  if (hasChild) {
    // If it has child, we must download child file list first
    changeFileToDownload(current->getChild());
    startDownload();
  } else if (hasNext) {
    // If it has no child, but has next file, we must download next file
    changeFileToDownload(current->getNext());
    startDownload();
  } else {

    //
    // If it has no child and not next, but has parent file,
    // we must go to parent file (folder i mean), set childs to NULL
    // cause we already download child files and go to next file.
    //

    FileInfoList *first = current->getFirst();
    if (hasParent) {
      changeFileToDownload(first->getParent());
      m_toCopy->setChild(NULL, 0);

      gotoNext();
    } else {
      killOp();
    } // if / else
  } // if / else
} // void

void DownloadOperation::tryCalcInputFilesSize()
{
  FileInfoList *fil = m_toCopy;

  while (fil != NULL) {
    if (fil->getFileInfo()->isDirectory()) {
      StringStorage pathNoRoot;
      StringStorage pathToFile;

      fil->getAbsolutePath(&pathNoRoot, _T('/'));

      pathToFile.setString(m_pathToSourceRoot.getString());
      if (!pathToFile.endsWith(_T('/'))) {
        pathToFile.appendString(_T("/"));
      }
      pathToFile.appendString(pathNoRoot.getString());

      m_foldersToCalcSizeLeft++;
      m_sender->sendFolderSizeRequest(pathToFile.getString());
      m_state = DS_CALC_SIZE;
    } else {
      m_totalBytesToCopy += fil->getFileInfo()->getSize();
    }
    fil = fil->getNext();
  }
}

void DownloadOperation::killOp()
{
  //
  // If not files to download, than clear memory and
  // operation is finished
  //

  delete m_toCopy->getRoot();
  m_toCopy = NULL;

  m_state = DS_FINISHED;

  notifyFinish();
}

void DownloadOperation::decFoldersToCalcSizeCount()
{
  m_foldersToCalcSizeLeft--;

  // No more folders to calc size, start download
  if (m_foldersToCalcSizeLeft == 0) {
    startDownload();
  }
}

void DownloadOperation::notifyFailedToDownload(const TCHAR *errorDescription)
{
  StringStorage message;

  message.format(_T("Error: failed to download '%s' (%s)"),
                 m_pathToSourceFile.getString(),
                 errorDescription);

  notifyError(message.getString());
}

void DownloadOperation::changeFileToDownload(FileInfoList *toDownload)
{
  m_toCopy = toDownload;

  getRemotePath(m_toCopy, m_pathToSourceRoot.getString(), &m_pathToSourceFile);
  m_isTargetPathSafe = getLocalPath(m_toCopy, m_pathToTargetRoot.getString(),
                                    &m_pathToTargetFile);
}

void DownloadOperation::refuseLocalTarget(const TCHAR *reason)
{
  StringStorage message;

  message.format(_T("Error: refused to download '%s': %s"),
                 m_pathToSourceFile.getString(), reason);

  notifyError(message.getString());

  // Download next file
  gotoNext();
}
