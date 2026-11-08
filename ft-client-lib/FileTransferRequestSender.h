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

#ifndef _FILE_TRANSFER_REQUEST_SENDER_H_
#define _FILE_TRANSFER_REQUEST_SENDER_H_

#include "util/inttypes.h"
#include "network/RfbOutputGate.h"
#include "io-lib/IOException.h"

#include "log-writer/LogWriter.h"

class FileTransferRequestSender
{
public:
  FileTransferRequestSender(LogWriter *logWriter);
  ~FileTransferRequestSender();

  void setOutput(RfbOutputGate *outputStream);

  void sendCompressionSupportRequest();
  void sendFileListRequest(const TCHAR *fullPath, bool useCompression);
  void sendDownloadRequest(const TCHAR *fullPathName, UINT64 offset);
  void sendDownloadDataRequest(UINT32 size, bool useCompression);
  void sendRmFileRequest(const TCHAR *fullPathName);
  void sendMkDirRequest(const TCHAR *fullPathName);
  void sendMvFileRequest(const TCHAR *oldFileName, const TCHAR *newFileName);
  void sendUploadRequest(const TCHAR *fullPathName, bool overwrite, UINT64 offset);
  void sendUploadDataRequest(const char *buffer, UINT32 size, bool useCompression);
  void sendUploadEndRequest(UINT8 fileFlags, UINT64 modificationTime);
  void sendFolderSizeRequest(const TCHAR *fullPath);

protected:
  LogWriter *m_logWriter;
  RfbOutputGate *m_output;
};

#endif
