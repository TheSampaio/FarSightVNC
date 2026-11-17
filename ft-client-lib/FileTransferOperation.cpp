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

#include "FileTransferOperation.h"
#include "OperationEventListener.h"

#include "util/WindowsFileName.h"

FileTransferOperation::FileTransferOperation(LogWriter *logWriter)
: m_logWriter(logWriter),
  m_sender(0),
  m_replyBuffer(0),
  m_isTerminating(false)
{
}

FileTransferOperation::~FileTransferOperation()
{
}

void FileTransferOperation::setRequestSender(FileTransferRequestSender *sender)
{
  m_sender = sender;
}

void FileTransferOperation::setReplyBuffer(FileTransferReplyBuffer *replyBuffer)
{
  m_replyBuffer = replyBuffer;
}

void FileTransferOperation::terminate()
{
  m_isTerminating = true;
}

bool FileTransferOperation::isTerminating()
{
  return m_isTerminating;
}

bool FileTransferOperation::acceptsReply(UINT32 rfbMessage)
{
  return true;
}

void FileTransferOperation::notifyStart()
{
  AutoLock al(&m_listeners);

  vector<OperationEventListener *>::iterator it;
  for (it = m_listeners.begin(); it != m_listeners.end(); it++) {
    OperationEventListener *l = *it;
    l->ftOpStarted(this);
  }
}

void FileTransferOperation::notifyFinish()
{
  AutoLock al(&m_listeners);

  vector<OperationEventListener *>::iterator it;
  for (it = m_listeners.begin(); it != m_listeners.end(); it++) {
    OperationEventListener *l = *it;
    l->ftOpFinished(this);
  }
}

void FileTransferOperation::notifyError(const TCHAR *message)
{
  m_logWriter->message(_T("%s\n"), message);

  AutoLock al(&m_listeners);

  vector<OperationEventListener *>::iterator it;
  for (it = m_listeners.begin(); it != m_listeners.end(); it++) {
    OperationEventListener *l = *it;
    l->ftOpErrorMessage(this, message);
  }
}

void FileTransferOperation::notifyInformation(const TCHAR *message)
{
  m_logWriter->message(_T("%s\n"), message);

  AutoLock al(&m_listeners);

  vector<OperationEventListener *>::iterator it;
  for (it = m_listeners.begin(); it != m_listeners.end(); it++) {
    OperationEventListener *l = *it;
    l->ftOpInfoMessage(this, message);
  }
}

bool FileTransferOperation::getLocalPath(FileInfoList *currentFile,
                                         const TCHAR *localFolder,
                                         StringStorage *out)
{
  StringStorage pathNoRoot;
  currentFile->getAbsolutePath(&pathNoRoot, _T('\\'));
  out->setString(localFolder);
  if (!out->endsWith(_T('\\')) && !pathNoRoot.beginsWith(_T('\\'))) {
    out->appendString(_T("\\"));
  }
  out->appendString(pathNoRoot.getString());

  //
  // The folder the operator picked is the boundary of the operation: a path
  // built out of remote names that canonicalizes anywhere else must not be
  // used, however it got that way. The check fails closed, so a path that
  // cannot even be canonicalized is refused too.
  //
  if (!WindowsFileName::isPathInsideDirectory(out->getString(), localFolder)) {
    m_logWriter->warning(_T("Refused the local path '%s': it is not inside ")
                         _T("the folder '%s'\n"),
                         out->getString(), localFolder);
    out->setString(_T(""));
    return false;
  }

  return true;
}

void FileTransferOperation::getRemotePath(FileInfoList *currentFile,
                                          const TCHAR *remoteFolder,
                                          StringStorage *out)
{
  StringStorage pathNoRoot;
  currentFile->getAbsolutePath(&pathNoRoot, _T('/'));

  out->setString(remoteFolder);
  if (!out->endsWith(_T('/')) && !pathNoRoot.beginsWith(_T('/'))) {
    out->appendString(_T("/"));
  }
  out->appendString(pathNoRoot.getString());
}
