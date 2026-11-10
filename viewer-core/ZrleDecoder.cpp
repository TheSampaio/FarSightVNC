// Copyright (C) 2011,2012 GlavSoft LLC.
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
// Modified for FarSightVNC on 2026-11-08: CPIXEL handling rewritten with
// explicit, bounds-checked byte offsets (RFC 6143 section 7.7.5); see the
// notes at ZrleDecoder::setPixelLayout().
//

#include "ZrleDecoder.h"

#include "io-lib/ByteArrayInputStream.h"

#include <vector>
#include <algorithm>

ZrleDecoder::ZrleDecoder(LogWriter *logWriter)
: DecoderOfRectangle(logWriter)
{
  m_encoding = EncodingDefs::ZRLE;
}

ZrleDecoder::~ZrleDecoder()
{
}

void ZrleDecoder::decode(RfbInputGate *input,
                         FrameBuffer *frameBuffer,
                         const Rect *dstRect)
{
  size_t maxUnpackedSize = getMaxSizeOfRectangle(dstRect);
  readAndInflate(input, maxUnpackedSize);

  size_t unpackedDataSize = m_inflater.getOutputSize();
  if (unpackedDataSize == 0) {
    m_logWriter->debug(_T("Empty unpacked data in ZRLE decoder"));
    if (dstRect->area() != 0) {
      throw Exception(_T("Bad data received from the server: Empty unpacked data in ZRLE decoder."));
    }
    return;
  }

  vector<unsigned char> unpackedData;
  unpackedData.resize(unpackedDataSize);
  unpackedData.assign(m_inflater.getOutput(), m_inflater.getOutput() + unpackedDataSize);
  ByteArrayInputStream unpackedByteArrayStream(reinterpret_cast<char *>(&unpackedData.front()),
                                               unpackedData.size());
  DataInputStream unpackedDataStream(&unpackedByteArrayStream);

  setPixelLayout(frameBuffer->getPixelFormat());

  for (int y = dstRect->top; y < dstRect->bottom; y += TILE_SIZE) {
    for (int x = dstRect->left; x < dstRect->right; x += TILE_SIZE) {
      Rect tileRect(x, y, 
                    std::min(x + TILE_SIZE, dstRect->right),
                    std::min(y + TILE_SIZE, dstRect->bottom));

      // FIXME: Check dstRect elsewhere on a higher level, once for all (non-pseudo) decoders,
      //        and document that in the corresponding interface contract. Do not check here
      //        or in other decoders. Also, placing this check in the tile loop is not necessary,
      //        it was enough to check once in the beginning of the function. I do not change that
      //        just to make sure I do not break anything. (-- const)
      if (!frameBuffer->getDimension().getRect().intersection(&tileRect).isEqualTo(&tileRect)) {
        throw Exception(_T("Incorrect size of ZRLE tile."));
      }
      size_t tileLength = tileRect.area();
      size_t tileBytesLength = tileLength * m_pixelSize;
      // Frame buffer pixels (m_pixelSize bytes each, zero-initialized).
      vector<char> pixels;
      pixels.resize(tileBytesLength);

      int type = readType(&unpackedDataStream);

      if (type == 0) {
        // raw pixel data
        readRawTile(&unpackedDataStream, pixels, &tileRect);
      } else if (type == 1) {
        // a solid tile consisting of a single colour
        readSolidTile(&unpackedDataStream, pixels, &tileRect);
      } else if (type >= 2 && type <= 16) {
        // packed palette
        readPackedPaletteTile(&unpackedDataStream, pixels, &tileRect, type);
      } else if (type >= 17 && type <= 127) {
        // Unused (no advantage over palette RLE). Kept as in the original
        // TightVNC code: no data is read and the tile is drawn black (all
        // zero pixels). Do not change without a decision on protocol
        // compatibility: a conforming server never sends these types.
      } else if (type == 128) {
        // plain rle
        readPlainRleTile(&unpackedDataStream, pixels, &tileRect);
      } else if (type == 129) {
        // invalid type
        StringStorage error;
        error.format(_T("Bad data received from the server: Unused ZRLE subencoding type (%d)."), type);
        throw Exception(error.getString());
      } else if (type >= 130 && type <= 255) {
        // palette rle
        readPaletteRleTile(&unpackedDataStream, pixels, &tileRect, type);
      }

      drawTile(frameBuffer, &tileRect, &pixels);
    } // tile(x, y)
  } // tile(..., y)
}

//
// Pixel layout (RFC 6143 section 7.7.5).
//
// A ZRLE tile stores CPIXELs. A CPIXEL is the frame buffer PIXEL, except for
// a 32 bpp true colour format whose colour bits all fit in the least
// significant 3 bytes or all in the most significant 3 bytes of the 32-bit
// pixel value: then the CPIXEL has only those 3 significant bytes and the
// empty byte is not sent. The CPIXEL bytes keep the byte order of the pixel
// (big-endian flag), so the position of the 3 bytes inside the 4 bytes of a
// pixel in memory is:
//
//   empty byte        little endian   big endian
//   most significant  bytes 0..2      bytes 1..3
//   least significant bytes 1..3      bytes 0..2
//
// The decoder keeps tiles and palettes as frame buffer pixels (m_pixelSize
// bytes, unused byte 0): CPIXELs are expanded when they are read, so the
// other code never sees the 3 byte form.
//
void ZrleDecoder::setPixelLayout(const PixelFormat &pxFormat)
{
  if (pxFormat.bitsPerPixel != 8 && pxFormat.bitsPerPixel != 16 &&
      pxFormat.bitsPerPixel != 32) {
    throw Exception(_T("Unsupported pixel format in ZRLE decoder (only 8, 16 or 32 bits per pixel)."));
  }

  m_pixelSize = pxFormat.bitsPerPixel / 8;
  m_cpixelSize = m_pixelSize;
  m_cpixelOffset = 0;

  if (pxFormat.bitsPerPixel == 32) {
    // A shift of 32 or more has no bits inside a 32-bit pixel (and shifting
    // by it is undefined): such a format keeps full 4 byte pixels.
    bool validShifts = pxFormat.redShift < 32 && pxFormat.greenShift < 32 &&
                       pxFormat.blueShift < 32;
    UINT32 colorMaxValue = 0xFFFFFFFF;
    if (validShifts) {
      colorMaxValue = (static_cast<UINT32>(pxFormat.blueMax)  << pxFormat.blueShift)  |
                      (static_cast<UINT32>(pxFormat.greenMax) << pxFormat.greenShift) |
                      (static_cast<UINT32>(pxFormat.redMax)   << pxFormat.redShift);
    }
    bool emptyMostSignificantByte = (colorMaxValue & 0xFF000000) == 0;
    bool emptyLeastSignificantByte = (colorMaxValue & 0x000000FF) == 0;
    if (emptyMostSignificantByte || emptyLeastSignificantByte) {
      // The most significant byte is the first in memory only if big endian.
      // (Both bytes empty: the original rule, most significant first, stays.)
      m_cpixelSize = 3;
      m_cpixelOffset = (emptyMostSignificantByte == pxFormat.bigEndian) ? 1 : 0;
    }
  }

  if (m_cpixelOffset + m_cpixelSize > m_pixelSize || m_pixelSize > sizeof(unsigned int)) {
    throw Exception(_T("Inconsistent pixel layout in ZRLE decoder."));
  }
}

void ZrleDecoder::expandPixel(const char *cpixel, char *pixel) const
{
  // setPixelLayout() guarantees m_cpixelOffset + m_cpixelSize <= m_pixelSize <= 4.
  memset(pixel, 0, m_pixelSize);
  memcpy(pixel + m_cpixelOffset, cpixel, m_cpixelSize);
}

void ZrleDecoder::readPixel(DataInputStream *input, char *pixel)
{
  char cpixel[4] = {0, 0, 0, 0};
  input->readFully(cpixel, m_cpixelSize);
  expandPixel(cpixel, pixel);
}

void ZrleDecoder::putPixel(vector<char> &pixels,
                           size_t indexPixel,
                           const char *pixel) const
{
  if ((indexPixel + 1) * m_pixelSize > pixels.size()) {
    throw Exception(_T("Bad data received from the server: ZRLE pixel is outside of the tile."));
  }
  memcpy(&pixels[indexPixel * m_pixelSize], pixel, m_pixelSize);
}

void ZrleDecoder::readAndInflate(RfbInputGate *input, size_t maximalUnpackedSize)
{
  UINT32 length = input->readUInt32();
  std::vector<char> zlibData;
  zlibData.resize(length);
  if (length == 0) {
    zlibData.resize(1);
  }
  input->readFully(&zlibData.front(), length);

  m_inflater.setInput(&zlibData.front(), length);
  m_inflater.setUnpackedSize(maximalUnpackedSize);
  m_inflater.inflate();
}

size_t ZrleDecoder::getMaxSizeOfRectangle(const Rect *dstRect)
{
  size_t widthCount = (dstRect->getWidth() + TILE_SIZE - 1) / TILE_SIZE;
  size_t heightCount = (dstRect->getHeight() + TILE_SIZE - 1) / TILE_SIZE;
  size_t tileCount = widthCount * heightCount;
  return TILE_LENGTH_SIZE + MAXIMAL_TILE_SIZE * tileCount;
}

int ZrleDecoder::readType(DataInputStream *input)
{
  int type = input->readUInt8();
  return type;
}

size_t ZrleDecoder::readRunLength(DataInputStream *input)
{
  size_t runLength = 0;
  UINT8 delta;
  do {
    delta = input->readUInt8();
    runLength += delta;
  } while (delta == 255); // if value == 255 then continue reading run-length
  return runLength + 1; // the length is one more than the sum
}

void ZrleDecoder::readPalette(DataInputStream *input,
                              const int paletteSize,
                              Palette *palette)
{
  palette->resize(paletteSize);

  for (int i = 0; i < paletteSize; i++) {
    // An entry is a frame buffer pixel in its first m_pixelSize bytes.
    (*palette)[i] = 0;
    readPixel(input, reinterpret_cast<char *>(&(*palette)[i]));
  }
}

void ZrleDecoder::readRawTile(DataInputStream *input,
                              vector<char> &pixels,
                              const Rect *tileRect)
{
  size_t tileLength = tileRect->area();
  if (pixels.size() != tileLength * m_pixelSize) {
    throw Exception(_T("Inconsistent tile buffer in ZRLE decoder."));
  }

  if (m_cpixelSize == m_pixelSize) {
    input->readFully(&pixels.front(), pixels.size());
    return;
  }

  // Expand the CPIXELs (3 bytes) into frame buffer pixels (4 bytes).
  vector<char> cpixels;
  cpixels.resize(tileLength * m_cpixelSize);
  input->readFully(&cpixels.front(), cpixels.size());
  for (size_t i = 0; i < tileLength; i++) {
    expandPixel(&cpixels[i * m_cpixelSize], &pixels[i * m_pixelSize]);
  }
}

void ZrleDecoder::readSolidTile(DataInputStream *input,
                                vector<char> &pixels,
                                const Rect *tileRect)
{
  size_t tileLength = tileRect->area();
  char solid[4] = {0, 0, 0, 0};

  readPixel(input, solid);

  // TODO: Can we optimize this?
  for (size_t i = 0; i < tileLength; i++) {
    putPixel(pixels, i, solid);
  }
}

void ZrleDecoder::readPackedPaletteTile(DataInputStream *input,
                                        vector<char> &pixels,
                                        const Rect *tileRect,
                                        const int type)
{
  int width = tileRect->getWidth();
  int height = tileRect->getHeight();

  // type and palette size is equal
  int paletteSize = type;
  Palette palette;
  readPalette(input, paletteSize, &palette);

  int m = 0;
  unsigned char mask = 0;
  unsigned char deltaOffset = 0;
  if (paletteSize == 2) {
    m = (width + 7) / 8;
    mask = 0x01;
    deltaOffset = 1;
  } else if (paletteSize == 3 || paletteSize == 4) {
    m = (width + 3) / 4;
    mask = 0x03;
    deltaOffset = 2;
  } else if (paletteSize >= 5 && paletteSize <= 16) {
    m = (width + 1) / 2;
    mask = 0x0F;
    deltaOffset = 4;
  }

  for (int y = 0; y < height; y++) {
    // bit lenght of UINT8
    unsigned char offset = 8;
    int index = 0;
    // FIXME: optimization. Read by line.
    int entryByIndex = input->readUInt8();

    for (int x = 0; x < width; x++) {
      offset -= deltaOffset;
      int color = (entryByIndex >> offset) & mask;
      if (offset == 0) {
        offset = 8;
        // Don't read next entry, if it's last pixel in tile.
        if (x != width - 1) {
          entryByIndex = input->readUInt8();
        }
      }
      if (color > palette.size() - 1) {
        throw Exception(_T("Bad data received from the server: ZRLE color index exceeds palette size."));
      }

      size_t count = static_cast<size_t>(y) * width + x;
      putPixel(pixels, count, reinterpret_cast<const char *>(&palette[color]));
    }
  }
}

void ZrleDecoder::readPlainRleTile(DataInputStream *input,
                                   vector<char> &pixels,
                                   const Rect *tileRect)
{
  size_t tileLength = tileRect->area();
  for (size_t indexPixel = 0; indexPixel < tileLength;) {
    char color[4] = {0, 0, 0, 0};
    readPixel(input, color);

    size_t runLength = readRunLength(input);
    if (runLength > tileLength - indexPixel) {
      throw Exception(_T("Bad data received from the server: ZRLE run length is too long in plain RLE tile."));
    }

    for (size_t i = 0; i < runLength; i++) {
      putPixel(pixels, indexPixel + i, color);
    }
    indexPixel += runLength;
  }
}

void ZrleDecoder::readPaletteRleTile(DataInputStream *input,
                                     vector<char> &pixels,
                                     const Rect *tileRect,
                                     const int type)
{
  size_t tileLength = tileRect->area();

  int paletteSize = type - 128;
  Palette palette;
  readPalette(input, paletteSize, &palette);

  for (size_t indexPixel = 0; indexPixel < tileLength;) {
    UINT8 color = input->readUInt8();

    size_t runLength = 1;
    if (color >= 128) {
      color -= 128;
      runLength = readRunLength(input);
      if (indexPixel + runLength > tileLength) {
        throw Exception(_T("Bad data received from the server: ZRLE run length is too long in palette RLE tile."));
      }
    }
    if (color > palette.size() - 1) {
      throw Exception(_T("Bad data received from the server: ZRLE color index exceeds palette size."));
    }

    for(size_t i = 0; i < runLength; i++) {
      putPixel(pixels, indexPixel + i, reinterpret_cast<const char *>(&palette[color]));
    }
    indexPixel += runLength;
  }
}

void ZrleDecoder::drawTile(FrameBuffer *fb,
                           const Rect *tileRect,
                           const vector<char> *pixels)
{
  int width = tileRect->getWidth();
  int height = tileRect->getHeight();
  size_t rowBytes = static_cast<size_t>(width) * m_pixelSize;

  // The tile was checked against the frame buffer dimension by decode(); the
  // tile buffer holds frame buffer pixels (see setPixelLayout()).
  if (fb->getBytesPerPixel() != m_pixelSize ||
      pixels->size() != rowBytes * height) {
    throw Exception(_T("Inconsistent tile buffer in ZRLE decoder."));
  }

  const char *pixelsPtr = &pixels->front();
  for (int row = 0; row < height; row++) {
    char *bufferPtr = (char*)fb->getBufferPtr(tileRect->left, tileRect->top + row);
    memcpy(bufferPtr, pixelsPtr, rowBytes);
    pixelsPtr += rowBytes;
  }
}
