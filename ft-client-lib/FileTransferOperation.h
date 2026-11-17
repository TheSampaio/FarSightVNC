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

#ifndef _FILE_TRANSFER_OPERATION_H_
#define _FILE_TRANSFER_OPERATION_H_

#include "FileTransferEventAdapter.h"
#include "OperationNotPermittedException.h"
#include "FileTransferRequestSender.h"
#include "FileTransferReplyBuffer.h"
#include "FileInfoList.h"

#include "log-writer/LogWriter.h"
#include "util/ListenerContainer.h"

class OperationEventListener;

//
// Empty file transfer operation class that do nothing and
// reject all messages (FileTransferEventAdapter policy by default).
//
// Class will throw OperationNotPermitted exception when caller
// tryies to use it as event handler for file transfer messages.
//

class FileTransferOperation : public FileTransferEventAdapter,
                              public ListenerContainer<OperationEventListener *>
{
public:
  FileTransferOperation(LogWriter *logWriter);
  virtual ~FileTransferOperation();

  //
  // Sets request sender member
  //

  void setRequestSender(FileTransferRequestSender *sender);

  //
  // Sets reply buffer member
  //

  void setReplyBuffer(FileTransferReplyBuffer *replyBuffer);

  //
  // Start executing of operation.
  // Method must be overrided by child classes.
  //

  virtual void start() = 0;

  //
  // Sets terminating flag to true in this operation.
  //
  // Remark: subclasses of FileTransferOperation must finish
  // execution when protected isTerminating method returns true.
  //

  virtual void terminate();

  //
  // Tells whether a reply carrying this message code belongs to the state the
  // operation is in right now.
  //
  // The message processor dispatches on the message code alone, so without
  // this a server can answer a request that was never sent, from the first
  // byte of the session on, and the handler then works on members that no
  // request has set up yet. An operation that keeps track of what it asked for
  // overrides this and refuses anything else; the reply is then dropped, not
  // acted upon.
  //
  // The default accepts every code, which is what the operations that keep no
  // state did before: they still refuse the codes they do not handle at all
  // through FileTransferEventAdapter.
  //

  virtual bool acceptsReply(UINT32 rfbMessage);

protected:

  //
  // Returns value of m_isTerminating flag
  //

  virtual bool isTerminating();

  //
  // Some helper methods that will be used by child classes.
  //

  //
  // Notify all listeners that operation has started.
  //

  void notifyStart();

  //
  // Notify all listeners that operation has finished.
  //

  void notifyFinish();

  //
  // Notify all listeners that was error during operation execution
  //

  void notifyError(const TCHAR *message);

  //
  // Notify all listeners with text message
  //

  void notifyInformation(const TCHAR *message);

  //
  // Sets absolute path to @currentFile, using @localFolder string as root folder
  // to @out parameter, '\' character used as separator for splitting directories.
  //
  // The path is built from the names held by the file info list, and in a
  // download every one of those names was chosen by the remote peer. The names
  // are already checked where they are parsed, so this is the second line of
  // defence: the finished path is canonicalized and must sit strictly below
  // @localFolder, the folder the operator picked.
  //
  // @return false when the path leaves @localFolder. @out is then set to the
  // empty string, so that even a caller that ignored the result cannot open
  // anything with it, but every caller must check and must not continue.
  //

  bool getLocalPath(FileInfoList *currentFile, const TCHAR *localFolder, StringStorage *out);

  //
  // Sets absolute path to @currentFile, using @remoteFolder string as root folder
  // to @out parameter, '/' character used as separator for splitting directories.
  //

  void getRemotePath(FileInfoList *currentFile, const TCHAR *remoteFolder, StringStorage *out);

protected:
  LogWriter *m_logWriter;

  FileTransferRequestSender *m_sender;
  FileTransferReplyBuffer *m_replyBuffer;

  bool m_isTerminating;
};

#endif
