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

#ifndef _DATA_INPUT_STREAM_H_
#define _DATA_INPUT_STREAM_H_

#include "util/inttypes.h"
#include "InputStream.h"
#include "IOException.h"

#include <vector>

/**
 * Data input stream class (decorator pattern).
 * Allows to read typed data from input stream.
 */
class DataInputStream : public InputStream
{
public:
  /**
   * Creates new DataInputStream.
   * @param inputStream input stream that will be used to read data.
   */
  DataInputStream(InputStream *inputStream);
  virtual ~DataInputStream();

  /**
   * Inherited from superclass.
   * Delegates execution of method to real input stream.
   */
  virtual size_t read(void *buffer, size_t len);

  virtual size_t available();

  /**
   * Reads exactly specified count of bytes from input stream.
   * @param buffer [out] target buffer to receive data.
   * @param len count of bytes to read.
   * @throws IOException on error.
   * @fixme really it can throw any kind of exception.
   */
  void readFully(void *buffer, size_t len);

  /**
   * Reads exactly size bytes into *buffer (replacing its contents) in blocks
   * of READ_BLOCK_BYTES, so that the memory grows with the bytes that really
   * arrived and not with the size that a peer declared.
   * @throws IOException on error.
   */
  void readFullyInBlocks(std::vector<char> *buffer, size_t size);

  UINT8 readUInt8();
  UINT16 readUInt16();
  UINT32 readUInt32();
  UINT64 readUInt64();

  INT8 readInt8();
  INT16 readInt16();
  INT32 readInt32();
  INT64 readInt64();

  /**
   * Reads a string that is preceded by its size in bytes (UINT32).
   * The size is checked against maxSizeInBytes before anything is read or
   * allocated. The caller knows what the string is, so the caller owns the
   * limit; the one-argument version uses DEFAULT_MAX_UTF8_BYTES, a backstop
   * and not a policy.
   * @throws Exception (not IOException: it is a protocol error, not a lost
   *         connection) when the declared size is above the limit.
   * @throws IOException on error.
   */
  void readUTF8(StringStorage *storage);
  void readUTF8(StringStorage *storage, UINT32 maxSizeInBytes);

  // Block size of readFullyInBlocks().
  static constexpr size_t READ_BLOCK_BYTES = 64 * 1024;
  // Ceiling of the one-argument readUTF8().
  static constexpr UINT32 DEFAULT_MAX_UTF8_BYTES = 64 * 1024 * 1024;

protected:
  InputStream *m_inputStream;
};

#endif
