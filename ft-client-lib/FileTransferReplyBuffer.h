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

#ifndef _FILE_TRANSFER_REPLY_BUFFER_H_
#define _FILE_TRANSFER_REPLY_BUFFER_H_

#include "FileTransferEventHandler.h"

#include "io-lib/DataInputStream.h"

#include "ft-common/FileInfo.h"
#include "util/Inflater.h"
#include "util/ZLibException.h"

#include "ft-common/OperationNotSupportedException.h"

#include "log-writer/LogWriter.h"

class FileTransferReplyBuffer : public FileTransferEventHandler
{
public:

  //
  // The ceilings on the numbers a reply carries. Every length and every count
  // in a file transfer reply is chosen by the remote peer, and the peer chooses
  // them before a single byte of the data they describe has been received, so
  // each one is checked before it is allowed to size an allocation, a read or
  // a write.
  //

  //
  // The largest number of entries one FILE_LIST_REPLY may announce.
  //
  // The reply describes the contents of one directory, not of a whole disk: a
  // directory with a million entries is already far past anything the file
  // transfer dialog can show or an operator can work with, and the real bound
  // is MIN_FILE_LIST_ENTRY_SIZE below, which requires the bytes of the entries
  // to have actually arrived (a million entries need at least 22 MB of reply).
  // This ceiling only stops the degenerate case where the count is read before
  // any entry is: a count near 0xFFFFFFFF is otherwise an allocation of many
  // gigabytes. No real TightVNC server comes anywhere near it.
  //
  static const UINT32 MAX_FILE_LIST_ENTRIES = 1000000;

  //
  // The smallest number of bytes one entry of a file listing can occupy on the
  // wire: size (8) + modification time (8) + flags (2) + the length of the
  // UTF-8 name (4), with an empty name. The announced count is checked against
  // the bytes that really arrived divided by this, so a count can never exceed
  // what the reply could possibly contain.
  //
  static const size_t MIN_FILE_LIST_ENTRY_SIZE = 8 + 8 + 2 + 4;

  //
  // The largest compressed or uncompressed block one reply may carry. The
  // viewer never asks for more than 400000 bytes of file data at a time, and a
  // file listing of one directory stays far below this; 64 MB leaves a very
  // wide margin over anything a TightVNC server sends while keeping a single
  // malformed length from asking for an allocation the process cannot serve.
  //
  static const UINT32 MAX_DATA_BLOCK_SIZE = 64 * 1024 * 1024;

  FileTransferReplyBuffer(LogWriter *logWriter);
  virtual ~FileTransferReplyBuffer();

  void getLastErrorMessage(StringStorage *storage);

  bool isCompressionSupported();

  UINT32 getFilesInfoCount();
  FileInfo *getFilesInfo();

  //
  // Returns how many entries of the last file listing were dropped because the
  // name the peer sent was not a usable single name component. A listing that
  // drops an entry stays usable; the count is there so that the condition can
  // be seen in the log and in the tests instead of only being guessed from a
  // short listing.
  //
  UINT32 getRejectedFilesInfoCount();

  UINT32 getDownloadBufferSize();
  vector<UINT8> getDownloadBuffer();

  UINT8 getDownloadFileFlags();
  UINT64 getDownloadLastModified();

  UINT64 getDirSize();

  //
  // Inherited from FileTransferEventHandler abstract class
  //

  virtual void onCompressionSupportReply(DataInputStream *input);
  virtual void onFileListReply(DataInputStream *input);
  virtual void onMd5DataReply(DataInputStream *input);

  virtual void onUploadReply(DataInputStream *input);
  virtual void onUploadDataReply(DataInputStream *input);
  virtual void onUploadEndReply(DataInputStream *input);

  virtual void onDownloadReply(DataInputStream *input);
  virtual void onDownloadDataReply(DataInputStream *input);
  virtual void onDownloadEndReply(DataInputStream *input);

  virtual void onMkdirReply(DataInputStream *input);
  virtual void onRmReply(DataInputStream *input);
  virtual void onMvReply(DataInputStream *input);

  virtual void onDirSizeReply(DataInputStream *input);
  virtual void onLastRequestFailedReply(DataInputStream *input);

private:

  vector<UINT8> readCompressedDataBlock(DataInputStream *input,
                                        UINT32 compressedSize,
                                        UINT32 uncompressedSize,
                                        UINT8 compressionLevel);

protected:

  //
  // Interface of log writer for log data
  //
  LogWriter *m_logWriter;

  //
  // ZLib stream for decompression of compressed data
  //
  Inflater m_inflater;

  //
  // Members than have access from public methods
  //

  // Compression support reply
  bool m_isCompressionSupported;

  // File list reply
  UINT32 m_filesInfoCount;
  FileInfo *m_filesInfo;

  // Entries of the last file list reply that were dropped by the name check
  UINT32 m_rejectedFilesInfoCount;

  // Last request message failed reply
  StringStorage m_lastErrorMessage;

  // Download data reply
  vector<UINT8> m_downloadBuffer;
  UINT32 m_downloadBufferSize;

  // Download end reply
  UINT8 m_downloadFileFlags;
  UINT64 m_downloadLastModified;

  // Dirsize reply data
  UINT64 m_dirSize;
};

#endif
