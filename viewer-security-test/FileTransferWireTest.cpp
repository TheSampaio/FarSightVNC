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
// Deterministic tests of the file transfer reply parsing: every length and
// every count in a reply is chosen by the remote peer, so none of them may
// size a read, a write or an allocation without being checked against what
// was actually received, and a reply that arrives out of order must not be
// acted upon.
//
// Covers the audit findings D-02, D-04 and D-05.
//
// No network and no VNC server are involved: the bodies of the replies are
// built byte by byte and read back through an in-memory stream, the same way
// viewer-clipboard-files-test drives the clipboard messages.
//

#include "viewer-security-test/SecurityTestHarness.h"

#include "ft-client-lib/DownloadOperation.h"
#include "ft-client-lib/FileTransferMessageProcessor.h"
#include "ft-client-lib/FileTransferReplyBuffer.h"
#include "ft-client-lib/OperationEventListener.h"
#include "ft-common/FTMessage.h"
#include "io-lib/DataInputStream.h"
#include "io-lib/InputStream.h"
#include "log-writer/LogWriter.h"
#include "util/Exception.h"

#include <stdio.h>
#include <string>
#include <vector>

namespace {

//----------------------------------------------------------------------------
// An in-memory stream and a writer for the reply bodies.

class MemoryInputStream : public InputStream
{
public:
  MemoryInputStream(const std::string &data) : m_data(data), m_position(0) {}

  virtual size_t read(void *buffer, size_t len)
  {
    size_t left = m_data.size() - m_position;
    if (left == 0) {
      throw Exception(_T("memory stream: no data left"));
    }
    size_t count = len < left ? len : left;
    memcpy(buffer, m_data.data() + m_position, count);
    m_position += count;
    return count;
  }

  virtual size_t available() { return m_data.size() - m_position; }

private:
  std::string m_data;
  size_t m_position;
};

// Builds the body of a reply in the byte order of the protocol.
class Body
{
public:
  void putUInt8(UINT8 value)
  {
    m_data.push_back((char)value);
  }

  void putUInt16(UINT16 value)
  {
    putUInt8((UINT8)(value >> 8));
    putUInt8((UINT8)(value & 0xFF));
  }

  void putUInt32(UINT32 value)
  {
    putUInt16((UINT16)(value >> 16));
    putUInt16((UINT16)(value & 0xFFFF));
  }

  void putUInt64(UINT64 value)
  {
    putUInt32((UINT32)(value >> 32));
    putUInt32((UINT32)(value & 0xFFFFFFFF));
  }

  // A name, as readUTF8 expects it: the length in bytes, then the bytes.
  void putUtf8(const char *name)
  {
    size_t length = strlen(name);
    putUInt32((UINT32)length);
    m_data.append(name, length);
  }

  void putBytes(const char *bytes, size_t length)
  {
    m_data.append(bytes, length);
  }

  void putFill(char byte, size_t count)
  {
    m_data.append(count, byte);
  }

  const std::string &data() const { return m_data; }

private:
  std::string m_data;
};

// Wraps one reply body in the two streams the parser is called with.
class Reply
{
public:
  Reply(const Body *body)
  : m_stream(body->data()), m_reader(&m_stream) {}

  DataInputStream *reader() { return &m_reader; }

private:
  MemoryInputStream m_stream;
  DataInputStream m_reader;
};

//----------------------------------------------------------------------------
// D-02: the length of a data block may never exceed the data that arrived.

void testDownloadDataLengths(SecurityTest::Counters *c)
{
  LogWriter logWriter(0);

  {
    //
    // The malformed reply from the audit: one byte of data is announced as
    // 65536 bytes of uncompressed data. Writing the announced size out of the
    // buffer that was really allocated copies the heap that follows it into
    // the file on the operator's disk.
    //
    Body body;
    body.putUInt8(0);        // compression level: none
    body.putUInt32(1);       // compressed size
    body.putUInt32(65536);   // uncompressed size
    body.putFill('A', 1);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool refused = false;
    try {
      replyBuffer.onDownloadDataReply(reply.reader());
    } catch (Exception &) {
      refused = true;
    }

    c->check(refused,
             "download data: a reply claiming more than it delivers is refused");
    c->check(replyBuffer.getDownloadBufferSize() <=
                 replyBuffer.getDownloadBuffer().size(),
             "download data: the reported size never exceeds the real buffer");
  }

  {
    // The reliable crash of the audit: a size close to 4 GB.
    Body body;
    body.putUInt8(0);
    body.putUInt32(1);
    body.putUInt32(0xFFFF0000);
    body.putFill('A', 1);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool refused = false;
    try {
      replyBuffer.onDownloadDataReply(reply.reader());
    } catch (Exception &) {
      refused = true;
    }

    c->check(refused, "download data: a 0xFFFF0000 size is refused");
    c->check(replyBuffer.getDownloadBufferSize() <=
                 replyBuffer.getDownloadBuffer().size(),
             "download data: the reported size stays sound after the refusal");
  }

  {
    //
    // The well-formed uncompressed reply a real TightVNC server sends: the two
    // sizes are equal and the data follows. It must still parse, byte for byte.
    //
    Body body;
    body.putUInt8(0);
    body.putUInt32(4);
    body.putUInt32(4);
    body.putBytes("data", 4);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool thrown = false;
    try {
      replyBuffer.onDownloadDataReply(reply.reader());
    } catch (Exception &) {
      thrown = true;
    }

    c->check(!thrown, "download data: a well-formed reply still parses");
    c->check(replyBuffer.getDownloadBufferSize() == 4,
             "download data: the size of a well-formed reply is the data size");

    vector<UINT8> buffer = replyBuffer.getDownloadBuffer();
    c->check(buffer.size() == 4 && buffer[0] == 'd' && buffer[3] == 'a',
             "download data: the data of a well-formed reply is intact");
  }

  {
    // An empty block is how the server answers a request for zero bytes.
    Body body;
    body.putUInt8(0);
    body.putUInt32(0);
    body.putUInt32(0);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool thrown = false;
    try {
      replyBuffer.onDownloadDataReply(reply.reader());
    } catch (Exception &) {
      thrown = true;
    }

    c->check(!thrown, "download data: an empty block still parses");
    c->check(replyBuffer.getDownloadBufferSize() == 0,
             "download data: an empty block reports no data");
  }
}

//----------------------------------------------------------------------------
// The file listing: the same length mismatch, the entry names and the count.

// Appends one entry of a file listing to the payload of a FILE_LIST_REPLY.
void putEntry(Body *payload, const char *name, UINT64 size, UINT16 flags)
{
  payload->putUInt64(size);
  payload->putUInt64(0);       // modification time
  payload->putUInt16(flags);
  payload->putUtf8(name);
}

// Wraps a listing payload in the uncompressed envelope of a FILE_LIST_REPLY.
void putListing(Body *body, const Body *payload)
{
  body->putUInt8(0);                           // compression level: none
  body->putUInt32((UINT32)payload->data().size());
  body->putUInt32((UINT32)payload->data().size());
  body->putBytes(payload->data().data(), payload->data().size());
}

void testFileListLengths(SecurityTest::Counters *c)
{
  LogWriter logWriter(0);

  {
    //
    // The same mismatch as the download data reply, on the listing: the stream
    // over the buffer used to be built with the announced size, so the parser
    // read entries out of the heap that follows the data.
    //
    Body payload;
    putEntry(&payload, "a.txt", 10, 0);

    Body body;
    body.putUInt8(0);
    body.putUInt32((UINT32)payload.data().size());
    body.putUInt32((UINT32)payload.data().size() + 65536);
    body.putBytes(payload.data().data(), payload.data().size());

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool refused = false;
    try {
      replyBuffer.onFileListReply(reply.reader());
    } catch (Exception &) {
      refused = true;
    }

    c->check(refused, "file list: a reply whose two sizes disagree is refused");
  }

  {
    // The well-formed listing a real server sends.
    Body payload;
    payload.putUInt32(2);
    putEntry(&payload, "a.txt", 10, 0);
    putEntry(&payload, "sub", 0, FileInfo::DIRECTORY);

    Body body;
    putListing(&body, &payload);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool thrown = false;
    try {
      replyBuffer.onFileListReply(reply.reader());
    } catch (Exception &) {
      thrown = true;
    }

    c->check(!thrown, "file list: a well-formed listing still parses");
    c->check(replyBuffer.getFilesInfoCount() == 2,
             "file list: a well-formed listing keeps both entries");
    c->check(replyBuffer.getRejectedFilesInfoCount() == 0,
             "file list: a well-formed listing rejects nothing");

    if (replyBuffer.getFilesInfoCount() == 2) {
      FileInfo *entries = replyBuffer.getFilesInfo();
      c->check(_tcscmp(entries[0].getFileName(), _T("a.txt")) == 0,
               "file list: the first name is kept");
      c->check(entries[0].getSize() == 10,
               "file list: the size of the first entry is kept");
      c->check(entries[1].isDirectory(),
               "file list: the directory flag is kept");
    } else {
      c->fail("file list: the well-formed listing is not readable");
    }
  }

  {
    //
    // A listing that carries a traversal name: the entry is dropped, the
    // listing stays usable and the session is not killed over it.
    //
    Body payload;
    payload.putUInt32(3);
    putEntry(&payload, "a.txt", 10, 0);
    putEntry(&payload,
             "..\\..\\..\\Users\\victim\\AppData\\Roaming\\Microsoft\\Windows"
             "\\Start Menu\\Programs\\Startup\\x.lnk", 1, 0);
    putEntry(&payload, "b.txt", 20, 0);

    Body body;
    putListing(&body, &payload);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool thrown = false;
    try {
      replyBuffer.onFileListReply(reply.reader());
    } catch (Exception &) {
      thrown = true;
    }

    c->check(!thrown, "file list: a hostile entry does not kill the listing");
    c->check(replyBuffer.getFilesInfoCount() == 2,
             "file list: only the hostile entry is dropped");
    c->check(replyBuffer.getRejectedFilesInfoCount() == 1,
             "file list: the dropped entry is counted");

    if (replyBuffer.getFilesInfoCount() == 2) {
      FileInfo *entries = replyBuffer.getFilesInfo();
      c->check(_tcscmp(entries[0].getFileName(), _T("a.txt")) == 0 &&
                   _tcscmp(entries[1].getFileName(), _T("b.txt")) == 0,
               "file list: the entries around the hostile one are kept");
    } else {
      c->fail("file list: the surviving entries are not readable");
    }
  }
}

void testFileListCounts(SecurityTest::Counters *c)
{
  LogWriter logWriter(0);

  {
    //
    // A count near 0xFFFFFFFF used to be an array of that many FileInfo
    // objects, allocated before a single entry had been received.
    //
    Body payload;
    payload.putUInt32(0xFFFFFFFF);
    putEntry(&payload, "a.txt", 10, 0);

    Body body;
    putListing(&body, &payload);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool refused = false;
    try {
      replyBuffer.onFileListReply(reply.reader());
    } catch (Exception &) {
      refused = true;
    }

    c->check(refused, "file list: a count of 0xFFFFFFFF is refused");
    c->check(replyBuffer.getFilesInfoCount() == 0,
             "file list: nothing is reported after a refused count");
  }

  {
    // One past the named ceiling, with the bytes still far too few.
    Body payload;
    payload.putUInt32(FileTransferReplyBuffer::MAX_FILE_LIST_ENTRIES + 1);
    putEntry(&payload, "a.txt", 10, 0);

    Body body;
    putListing(&body, &payload);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool refused = false;
    try {
      replyBuffer.onFileListReply(reply.reader());
    } catch (Exception &) {
      refused = true;
    }

    c->check(refused, "file list: a count over the ceiling is refused");
  }

  {
    //
    // Under the ceiling, but more entries than the bytes that arrived could
    // possibly hold: the count is checked against the data as well.
    //
    Body payload;
    payload.putUInt32(1000);
    putEntry(&payload, "a.txt", 10, 0);

    Body body;
    putListing(&body, &payload);

    FileTransferReplyBuffer replyBuffer(&logWriter);
    Reply reply(&body);

    bool refused = false;
    try {
      replyBuffer.onFileListReply(reply.reader());
    } catch (Exception &) {
      refused = true;
    }

    c->check(refused,
             "file list: a count larger than the received data is refused");
  }
}

//----------------------------------------------------------------------------
// D-04: a reply that does not belong to the state of the operation.

// Counts what the operation reports, so that "not acted upon" can be checked.
class OperationWatcher : public OperationEventListener
{
public:
  OperationWatcher() : m_finished(0), m_errors(0) {}

  virtual void ftOpStarted(FileTransferOperation *sender) {}
  virtual void ftOpFinished(FileTransferOperation *sender) { m_finished++; }
  virtual void ftOpErrorMessage(FileTransferOperation *sender,
                                const TCHAR *message) { m_errors++; }
  virtual void ftOpInfoMessage(FileTransferOperation *sender,
                               const TCHAR *message) {}

  int finished() const { return m_finished; }

private:
  int m_finished;
  int m_errors;
};

void testOutOfOrderReply(SecurityTest::Counters *c)
{
  LogWriter logWriter(0);

  FileInfo toDownload(10, 0, 0, _T("a.txt"));

  FileTransferReplyBuffer replyBuffer(&logWriter);

  DownloadOperation operation(&logWriter, &toDownload, 1,
                              _T("C:\\Downloads\\FarSightVNC"),
                              _T("/remote"));
  operation.setReplyBuffer(&replyBuffer);

  OperationWatcher watcher;
  operation.addListener(&watcher);

  //
  // The operation has sent nothing: nothing is outstanding, so no reply
  // belongs to it yet. The end of a download in particular closes the local
  // file, which is still null.
  //
  c->check(!operation.acceptsReply(FTMessage::DOWNLOAD_END_REPLY),
           "out of order: an end reply before any download is refused");
  c->check(!operation.acceptsReply(FTMessage::DOWNLOAD_DATA_REPLY),
           "out of order: a data reply before any download is refused");
  c->check(!operation.acceptsReply(FTMessage::FILE_LIST_REPLY),
           "out of order: a file list reply that was not asked for is refused");
  c->check(!operation.acceptsReply(FTMessage::DIRSIZE_REPLY),
           "out of order: a folder size reply that was not asked for is refused");

  //
  // The same thing through the dispatcher, which is the path a server message
  // really takes: the reply buffer still reads the body out of the stream, the
  // operation is skipped, and nothing happens to the operation.
  //
  FileTransferMessageProcessor processor;
  processor.addListener(&replyBuffer);
  processor.addListener(&operation);

  Body body;
  body.putUInt8(0);            // file flags
  body.putUInt64(1234567890);  // modification time

  Reply reply(&body);

  bool thrown = false;
  try {
    processor.processRfbMessage(reply.reader(), FTMessage::DOWNLOAD_END_REPLY);
  } catch (Exception &) {
    thrown = true;
  }

  c->check(!thrown,
           "out of order: dispatching an early end reply does not fail");
  c->check(watcher.finished() == 0,
           "out of order: the early end reply is not acted upon");
  c->check(replyBuffer.getDownloadLastModified() == 1234567890,
           "out of order: the body of the reply is still read from the stream");

  processor.removeListener(&operation);
  processor.removeListener(&replyBuffer);
  operation.removeListener(&watcher);
}

} // namespace

void runFileTransferWireTests(int *passed, int *failed)
{
  SecurityTest::Counters counters(passed, failed);

  testDownloadDataLengths(&counters);
  testFileListLengths(&counters);
  testFileListCounts(&counters);
  testOutOfOrderReply(&counters);
}
