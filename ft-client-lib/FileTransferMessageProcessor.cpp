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

#include "FileTransferMessageProcessor.h"
#include "FileTransferOperation.h"
#include "ft-common/FTMessage.h"

FileTransferMessageProcessor::FileTransferMessageProcessor()
{
}

FileTransferMessageProcessor::~FileTransferMessageProcessor()
{
}

void FileTransferMessageProcessor::processRfbMessage(DataInputStream *input,
                                                     UINT32 rfbMessage)
{
  AutoLock al(&m_listeners);

  for (size_t i = 0; i < m_listeners.size(); i++) {
    FileTransferEventHandler *listener = m_listeners.at(i);

    //
    // Dispatching on the message code alone lets a server answer a request
    // that was never sent: the handler then runs before the request that
    // prepares it, on members that are still null. An operation that keeps
    // track of what it asked for is asked first, and a reply that does not
    // belong to its current state is dropped instead of being acted upon. The
    // reply buffer is not an operation and keeps parsing every reply, so the
    // bytes of the message are still consumed from the stream.
    //
    FileTransferOperation *operation =
      dynamic_cast<FileTransferOperation *>(listener);

    if (operation != NULL && !operation->acceptsReply(rfbMessage)) {
      continue;
    }

    switch (rfbMessage) {
    case FTMessage::COMPRESSION_SUPPORT_REPLY:
      listener->onCompressionSupportReply(input);
      break;
    case FTMessage::FILE_LIST_REPLY:
      listener->onFileListReply(input);
      break;
    case FTMessage::DOWNLOAD_START_REPLY:
      listener->onDownloadReply(input);
      break;
    case FTMessage::DOWNLOAD_DATA_REPLY:
      listener->onDownloadDataReply(input);
      break;
    case FTMessage::DOWNLOAD_END_REPLY:
      listener->onDownloadEndReply(input);
      break;
    case FTMessage::UPLOAD_START_REPLY:
      listener->onUploadReply(input);
      break;
    case FTMessage::UPLOAD_DATA_REPLY:
      listener->onUploadDataReply(input);
      break;
    case FTMessage::UPLOAD_END_REPLY:
      listener->onUploadEndReply(input);
      break;
    case FTMessage::MD5_REPLY:
      listener->onMd5DataReply(input);
      break;
    case FTMessage::DIRSIZE_REPLY:
      listener->onDirSizeReply(input);
      break;
    case FTMessage::RENAME_REPLY:
      listener->onMvReply(input);
      break;
    case FTMessage::MKDIR_REPLY:
      listener->onMkdirReply(input);
      break;
    case FTMessage::REMOVE_REPLY:
      listener->onRmReply(input);
      break;
    case FTMessage::LAST_REQUEST_FAILED_REPLY:
      listener->onLastRequestFailedReply(input);
      break;
    } // switch
  } // for
}
