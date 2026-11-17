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

#include "FileTransferReplyBuffer.h"
#include "io-lib/ByteArrayInputStream.h"
#include "thread/AutoLock.h"
#include <crtdbg.h>

FileTransferReplyBuffer::FileTransferReplyBuffer(LogWriter *logWriter)
: m_logWriter(logWriter),
  m_isCompressionSupported(false),
  m_filesInfoCount(0), m_filesInfo(NULL),
  m_rejectedFilesInfoCount(0),
  m_downloadBufferSize(0),
  m_downloadFileFlags(0), m_downloadLastModified(0),
  m_dirSize(0)
{
  m_lastErrorMessage.setString(_T(""));
}

FileTransferReplyBuffer::~FileTransferReplyBuffer()
{
  if (m_filesInfo != NULL) {
    delete[] m_filesInfo;
  }
}

void FileTransferReplyBuffer::getLastErrorMessage(StringStorage *storage)
{
  *storage = m_lastErrorMessage;
}

bool FileTransferReplyBuffer::isCompressionSupported()
{
  return m_isCompressionSupported;
}

UINT32 FileTransferReplyBuffer::getFilesInfoCount()
{
  return m_filesInfoCount;
}

FileInfo *FileTransferReplyBuffer::getFilesInfo()
{
  return m_filesInfo;
}

UINT32 FileTransferReplyBuffer::getRejectedFilesInfoCount()
{
  return m_rejectedFilesInfoCount;
}

UINT32 FileTransferReplyBuffer::getDownloadBufferSize()
{
  return m_downloadBufferSize;
}

UINT8 FileTransferReplyBuffer::getDownloadFileFlags()
{
  return m_downloadFileFlags;
}

UINT64 FileTransferReplyBuffer::getDownloadLastModified()
{
  return m_downloadLastModified;
}

UINT64 FileTransferReplyBuffer::getDirSize()
{
  return m_dirSize;
}

vector<UINT8> FileTransferReplyBuffer::getDownloadBuffer()
{
  return m_downloadBuffer;
}

void FileTransferReplyBuffer::onCompressionSupportReply(DataInputStream *input)
{
  m_isCompressionSupported = (input->readUInt8() == 1);

  m_logWriter->info(_T("Received compression support reply: %s\n"),
                    m_isCompressionSupported ? _T("supported") : _T("not supported"));
}

void FileTransferReplyBuffer::onFileListReply(DataInputStream *input)
{
  UINT8 compressionLevel = 0;
  UINT32 compressedSize = 0;
  UINT32 uncompressedSize = 0;

  vector<UINT8> buffer;

  {
    compressionLevel = input->readUInt8();
    compressedSize = input->readUInt32();
    uncompressedSize = input->readUInt32();

    buffer = readCompressedDataBlock(input,
                                     compressedSize,
                                     uncompressedSize,
                                     compressionLevel);
  }

  if (!buffer.empty()) {
    //
    // The number of bytes that really arrived is the only length allowed to
    // size this stream. uncompressedSize is a number the peer chose, and when
    // the peer did not compress readCompressedDataBlock has already refused the
    // reply if the two sizes disagreed; reading through buffer.size() means
    // that no length from the wire can reach past the memory that exists.
    //
    // FIXME: type conversion in C-style
    ByteArrayInputStream memoryInputStream(reinterpret_cast<char *>(&buffer.front()),
                                           buffer.size());
    DataInputStream filesInfoReader(&memoryInputStream);

    if (m_filesInfo != 0) {
      delete[] m_filesInfo;
      m_filesInfo = 0;
    }
    m_filesInfoCount = 0;
    m_rejectedFilesInfoCount = 0;

    UINT32 announcedCount = filesInfoReader.readUInt32();

    //
    // The count is read before any entry is, so it must be checked against
    // what the protocol can really deliver before it sizes an allocation.
    //
    if (announcedCount > MAX_FILE_LIST_ENTRIES) {
      throw IOException(_T("Malformed file list reply: the announced file ")
                        _T("count is over the limit"));
    }
    if (announcedCount >
        (buffer.size() - sizeof(UINT32)) / MIN_FILE_LIST_ENTRY_SIZE) {
      throw IOException(_T("Malformed file list reply: the announced file ")
                        _T("count is larger than the received data can hold"));
    }

    if (announcedCount != 0) {
      //
      // Owned by the member from here on, so that an exception raised while
      // the entries are being read cannot leak the array.
      //
      m_filesInfo = new FileInfo[announcedCount];
    }

    UINT32 accepted = 0;

    for (UINT32 i = 0; i < announcedCount; i++) {
      UINT64 size = filesInfoReader.readUInt64();
      UINT64 lastModified = filesInfoReader.readUInt64();
      UINT16 flags = filesInfoReader.readUInt16();

      StringStorage t;
      filesInfoReader.readUTF8(&t);

      //
      // The name is the one thing in this reply that reaches the local
      // filesystem, so it is checked here, where it enters. A rejected entry
      // is dropped and the rest of the listing is kept: the listing stays
      // usable, the session is not killed over it, and no dialog is shown,
      // because a hostile server could otherwise make the viewer spam the
      // operator with one message box per entry.
      //
      FileInfo *fileInfo = &m_filesInfo[accepted];

      if (!fileInfo->setRemoteFileName(t.getString())) {
        m_rejectedFilesInfoCount++;
        m_logWriter->warning(_T("Refused a file list entry: the server sent ")
                             _T("the name '%s', which is not a usable file ")
                             _T("name\n"),
                             t.getString());
        continue;
      }

      fileInfo->setSize(size);
      fileInfo->setLastModified(lastModified);
      fileInfo->setFlags(flags);

      accepted++;
    } // for all newly created file's info

    m_filesInfoCount = accepted;

    if (m_rejectedFilesInfoCount != 0) {
      m_logWriter->warning(_T("Refused %d of %d entries of a file list reply\n"),
                           m_rejectedFilesInfoCount, announcedCount);
    }

    m_logWriter->info(_T("Received file list reply: \n")
                      _T("\t files count = %d\n")
                      _T("\t use compression = %d\n"),
                      m_filesInfoCount, compressionLevel);
  } else {
    m_logWriter->info(_T("Received file list reply is not read: ")
                      _T("compressed buffer is empty"));
  }
}

void FileTransferReplyBuffer::onMd5DataReply(DataInputStream *input)
{
  throw OperationNotSupportedException();
}

void FileTransferReplyBuffer::onUploadReply(DataInputStream *input)
{
  m_logWriter->info(_T("Received upload reply\n"));
}

void FileTransferReplyBuffer::onUploadDataReply(DataInputStream *input)
{
  m_logWriter->info(_T("Received upload data reply\n"));
}

void FileTransferReplyBuffer::onUploadEndReply(DataInputStream *input)
{
  m_logWriter->info(_T("Received upload end reply\n"));
}

void FileTransferReplyBuffer::onDownloadReply(DataInputStream *input)
{
  m_logWriter->info(_T("Received download reply\n"));
}

void FileTransferReplyBuffer::onDownloadDataReply(DataInputStream *input)
{
  UINT8 coLevel = input->readUInt8();
  UINT32 coBufferSize = input->readUInt32();
  UINT32 uncoBufferSize = input->readUInt32();

  m_downloadBuffer = readCompressedDataBlock(input, coBufferSize, uncoBufferSize, coLevel);
  //
  // The size of the buffer that was really produced, never a length the peer
  // chose: this number is what DownloadOperation writes out of the buffer, so
  // it must describe the buffer and nothing else. A reply that claims more
  // than it delivers was already refused by readCompressedDataBlock.
  //
  m_downloadBufferSize = (UINT32)m_downloadBuffer.size();

  m_logWriter->info(_T("Received download data reply:\n")
                    _T("\tcompressed size: %d\n")
                    _T("\tuncompressed size: %d\n")
                    _T("\tuse compression: %d\n"),
                    coBufferSize, uncoBufferSize, coLevel);
}

void FileTransferReplyBuffer::onDownloadEndReply(DataInputStream *input)
{
  m_downloadFileFlags = input->readUInt8();
  m_downloadLastModified = input->readUInt64();

  m_logWriter->info(_T("Received download end reply:\n")
                    _T("\tfile flags: %d\n")
                    _T("\tmodification time: %ld\n"),
                    m_downloadFileFlags, m_downloadLastModified);
}

void FileTransferReplyBuffer::onMkdirReply(DataInputStream *input)
{
  m_logWriter->info(_T("Received mkdir reply\n"));
}

void FileTransferReplyBuffer::onRmReply(DataInputStream *input)
{
  m_logWriter->info(_T("Received rm reply\n"));
}

void FileTransferReplyBuffer::onMvReply(DataInputStream *input)
{
  m_logWriter->info(_T("Received rename reply\n"));
}

void FileTransferReplyBuffer::onDirSizeReply(DataInputStream *input)
{
  m_dirSize = input->readUInt64();

  m_logWriter->info(_T("Received dirsize reply\n"));
}

void FileTransferReplyBuffer::onLastRequestFailedReply(DataInputStream *input)
{
  input->readUTF8(&m_lastErrorMessage);

  m_logWriter->info(_T("Received last request failed reply:\n")
                    _T("\terror message: %s\n"),
                    m_lastErrorMessage.getString());
}

vector<UINT8> FileTransferReplyBuffer::readCompressedDataBlock(DataInputStream *input,
                                                               UINT32 compressedSize,
                                                               UINT32 uncompressedSize,
                                                               UINT8 compressionLevel)
{
  //
  // Buffers with compressed and uncompressed data.
  // When not using compression uncoBuffer = coBuffer.
  //

  UINT32 coSize = compressedSize;
  UINT32 uncoSize = uncompressedSize;

  //
  // When the peer says it did not compress, the two sizes describe the very
  // same bytes, and a legitimate TightVNC server sends them equal. A
  // disagreement means the reply is malformed and neither number can be
  // trusted, so the reply is refused instead of being parsed with whichever
  // number is larger: trusting uncompressedSize would read and then write out
  // the heap that follows a buffer only compressedSize bytes long.
  //
  if (compressionLevel == 0 && uncoSize != coSize) {
    throw IOException(_T("Malformed file transfer reply: uncompressed data ")
                      _T("whose compressed and uncompressed sizes disagree"));
  }

  if (coSize > MAX_DATA_BLOCK_SIZE || uncoSize > MAX_DATA_BLOCK_SIZE) {
    throw IOException(_T("Malformed file transfer reply: the announced data ")
                      _T("block size is over the limit"));
  }

  vector<UINT8> coBuffer(coSize);

  //
  // Read compressed data
  //

  if (coSize == 0) {
    return coBuffer;
  }

  input->readFully(&coBuffer.front(), coSize);

  if (compressionLevel == 0) {
    return coBuffer;
  }

  vector<UINT8> uncoBuffer(uncoSize);

  m_inflater.setUnpackedSize(uncoSize);
  // FIXME: type conversion in C-style
  m_inflater.setInput((const char*)&coBuffer.front(), coSize);

  m_inflater.inflate();

  //
  // The memcpy below copies uncoSize bytes out of the inflater, so the
  // inflater must really hold that many. This was an _ASSERT, which checks
  // nothing in the shipped build and left the peer's uncompressedSize to drive
  // a read past the inflated data. A server that compresses correctly produces
  // exactly the size it announced.
  //
  if (m_inflater.getOutputSize() != uncoSize) {
    throw IOException(_T("Malformed file transfer reply: the inflated data ")
                      _T("is not as large as the reply announced"));
  }

  if (uncoSize != 0) {
    memcpy(&uncoBuffer.front(), m_inflater.getOutput(), uncoSize);
  }

  return uncoBuffer;
}
