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
// Deterministic tests of the limits applied to the RFB stream: counts and
// sizes that arrive from the server must have an explicit ceiling before
// they size an allocation or drive a loop, and a pixel format must be
// rejected when it would make the viewer divide by zero or shift past the
// width of a pixel.
//
// Covers the audit findings A-04, A-07, A-08, A-10, B-04, B-07 and C-04.
//
// The ceilings and the pixel format rule live in viewer-core/RfbLimits.h.
// readUTF8, the ZRLE decoder and the RRE decoder are driven here through
// in-memory streams. The clipboard length, the Tight capability counts, the
// pixel format and the desktop geometry are checked inside RemoteViewerCore
// (private methods of an object that needs a socket), so for those the tests
// exercise the predicates that the methods call; the wiring itself is covered
// by review, not by a test.
//

#include "viewer-security-test/SecurityTestHarness.h"

#include "io-lib/DataInputStream.h"
#include "io-lib/IOException.h"
#include "io-lib/InputStream.h"
#include "log-writer/LogWriter.h"
#include "network/RfbInputGate.h"
#include "region/Dimension.h"
#include "region/Rect.h"
#include "rfb/FrameBuffer.h"
#include "rfb/PixelFormat.h"
#include "rfb/StandardPixelFormatFactory.h"
#include "util/Exception.h"
#include "util/StringStorage.h"
#include "viewer-core/RfbLimits.h"
#include "viewer-core/RreDecoder.h"
#include "viewer-core/ZrleDecoder.h"

#include "vendor/zlib/zlib.h"

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

// An in-memory stream that reports how much was consumed, so a test can show
// that a refused value stopped the parser before it read or allocated the
// payload. Running out of data is an IOException, as for a closed socket; a
// limit error is a plain Exception, so the two cannot be mistaken.
class MemoryInputStream : public InputStream
{
public:
  explicit MemoryInputStream(const std::string &data) : m_data(data), m_position(0) {}

  virtual size_t read(void *buffer, size_t len)
  {
    size_t left = m_data.size() - m_position;
    if (left == 0) {
      throw IOException(_T("memory stream: no data left"));
    }
    size_t count = len < left ? len : left;
    memcpy(buffer, m_data.data() + m_position, count);
    m_position += count;
    return count;
  }

  virtual size_t available() { return m_data.size() - m_position; }
  size_t position() const { return m_position; }

private:
  std::string m_data;
  size_t m_position;
};

void putU16(std::string *out, unsigned v)
{
  out->push_back(static_cast<char>((v >> 8) & 0xFF));
  out->push_back(static_cast<char>(v & 0xFF));
}

void putU32(std::string *out, UINT32 v)
{
  putU16(out, (v >> 16) & 0xFFFF);
  putU16(out, v & 0xFFFF);
}

// What running a parser against a stream did: nothing wrong, a limit error
// (an Exception that is not an IOException) or the stream running dry.
enum Outcome { ACCEPTED, REFUSED, RAN_DRY };

//----------------------------------------------------------------------------
// readUTF8 (A-07).

Outcome readString(const std::string &wire, UINT32 limit, bool useDefaultLimit,
                   StringStorage *result, size_t *consumed)
{
  MemoryInputStream stream(wire);
  DataInputStream input(&stream);
  Outcome outcome = ACCEPTED;
  try {
    if (useDefaultLimit) {
      input.readUTF8(result);
    } else {
      input.readUTF8(result, limit);
    }
  } catch (const IOException &) {
    outcome = RAN_DRY;
  } catch (const Exception &) {
    outcome = REFUSED;
  }
  *consumed = stream.position();
  return outcome;
}

void testReadUtf8(SecurityTest::Counters &counters)
{
  std::string ordinary;
  putU32(&ordinary, 5);
  ordinary += "hello";
  StringStorage text;
  size_t consumed = 0;
  Outcome outcome = readString(ordinary, RfbLimits::MAX_AUTH_REASON_BYTES, false, &text, &consumed);
  counters.check(outcome == ACCEPTED && consumed == ordinary.size() &&
                 text.isEqualTo(_T("hello")),
                 "readUTF8: an ordinary string is still read");

  // The same string through the one-argument form.
  outcome = readString(ordinary, 0, true, &text, &consumed);
  counters.check(outcome == ACCEPTED && text.isEqualTo(_T("hello")),
                 "readUTF8: an ordinary string is read with the default limit");

  std::string empty;
  putU32(&empty, 0);
  outcome = readString(empty, RfbLimits::MAX_AUTH_REASON_BYTES, false, &text, &consumed);
  counters.check(outcome == ACCEPTED && text.isEmpty(),
                 "readUTF8: an empty string is still read");

  // A peer declares 4 GiB. Following bytes exist, so a reader that allocated
  // and read would not fail on the stream: only the limit can refuse this, and
  // only the 4 bytes of the size may have been consumed.
  std::string huge;
  putU32(&huge, 0xFFFFFFFFu);
  huge.append(100, 'x');
  outcome = readString(huge, RfbLimits::MAX_AUTH_REASON_BYTES, false, &text, &consumed);
  counters.check(outcome == REFUSED, "readUTF8: a declared 4 GiB is refused by the limit");
  counters.check(consumed == 4, "readUTF8: the refused string's payload is not read");

  outcome = readString(huge, 0, true, &text, &consumed);
  counters.check(outcome == REFUSED && consumed == 4,
                 "readUTF8: the default limit also refuses a declared 4 GiB");

  // The boundary of the caller's limit.
  std::string atLimit;
  putU32(&atLimit, 16);
  atLimit.append(16, 'a');
  outcome = readString(atLimit, 16, false, &text, &consumed);
  counters.check(outcome == ACCEPTED, "readUTF8: a string of exactly the limit is accepted");

  std::string overLimit;
  putU32(&overLimit, 17);
  overLimit.append(17, 'a');
  outcome = readString(overLimit, 16, false, &text, &consumed);
  counters.check(outcome == REFUSED && consumed == 4,
                 "readUTF8: a string one byte over the limit is refused");

  // The real authentication failure reason is a short sentence.
  counters.check(RfbLimits::MAX_AUTH_REASON_BYTES >= 1024,
                 "readUTF8: the authentication reason limit leaves room for a real reason");
}

void testReadInBlocks(SecurityTest::Counters &counters)
{
  // More than two blocks, to cover the block loop and the short last block.
  const size_t size = DataInputStream::READ_BLOCK_BYTES * 2 + 123;
  std::string payload(size, '\0');
  for (size_t i = 0; i < size; i++) {
    payload[i] = static_cast<char>((i * 31 + 7) & 0xFF);
  }
  MemoryInputStream stream(payload);
  DataInputStream input(&stream);
  std::vector<char> buffer;
  buffer.push_back('z'); // must be replaced, not appended to
  input.readFullyInBlocks(&buffer, size);
  counters.check(buffer.size() == size && memcmp(&buffer[0], payload.data(), size) == 0,
                 "readFullyInBlocks: a payload of several blocks arrives intact");

  // A peer that declares more than it sends: memory follows what arrived.
  MemoryInputStream shortStream(payload.substr(0, 100));
  DataInputStream shortInput(&shortStream);
  std::vector<char> shortBuffer;
  bool ranDry = false;
  try {
    shortInput.readFullyInBlocks(&shortBuffer, 0x7FFFFFFFu);
  } catch (const IOException &) {
    ranDry = true;
  }
  counters.check(ranDry && shortBuffer.capacity() < 1024 * 1024,
                 "readFullyInBlocks: a declared 2 GiB that never arrives allocates one block, not 2 GiB");
}

//----------------------------------------------------------------------------
// Clipboard (A-08), Tight capability counts (A-10), geometry (A-04).

void testClipboardLength(SecurityTest::Counters &counters)
{
  counters.check(RfbLimits::clipboardLengthFits(0) &&
                 RfbLimits::clipboardLengthFits(300 * 1024) &&
                 RfbLimits::clipboardLengthFits(RfbLimits::MAX_CLIPBOARD_BYTES),
                 "clipboard: ordinary lengths and the ceiling itself are accepted");
  counters.check(!RfbLimits::clipboardLengthFits(RfbLimits::MAX_CLIPBOARD_BYTES + 1) &&
                 !RfbLimits::clipboardLengthFits(0x80000000u) &&
                 !RfbLimits::clipboardLengthFits(0xFFFFFFFFu),
                 "clipboard: a length above the ceiling is refused");

  // The check that the viewer used to have: the length is a UINT32 widened to
  // size_t and compared with SIZE_MAX - 1. With a 64-bit size_t no UINT32 is
  // ever larger, so it refused nothing.
  counters.check(sizeof(size_t) == 8, "clipboard: the project is a 64-bit build");
  const UINT32 largestWireLength = 0xFFFFFFFFu;
  const size_t widened = largestWireLength;
  counters.check(!(widened > SIZE_MAX - 1),
                 "clipboard: the old 'length > SIZE_MAX - 1' check cannot refuse even 0xFFFFFFFF");
}

void testTightCapabilityCount(SecurityTest::Counters &counters)
{
  // A TightVNC server sends one or two tunnel types and one to three
  // authentication types.
  counters.check(RfbLimits::tightCapabilityCountFits(0) &&
                 RfbLimits::tightCapabilityCountFits(1) &&
                 RfbLimits::tightCapabilityCountFits(3) &&
                 RfbLimits::tightCapabilityCountFits(RfbLimits::MAX_TIGHT_CAPABILITIES),
                 "tight capabilities: real counts and the ceiling itself are accepted");
  counters.check(!RfbLimits::tightCapabilityCountFits(RfbLimits::MAX_TIGHT_CAPABILITIES + 1) &&
                 !RfbLimits::tightCapabilityCountFits(0xFFFFFFFFu),
                 "tight capabilities: a count above the ceiling is refused");
}

void testDesktopGeometry(SecurityTest::Counters &counters)
{
  const int limit = static_cast<int>(RfbLimits::MAX_DESKTOP_DIMENSION);
  counters.check(RfbLimits::fitsDesktopGeometry(1920, 1080) &&
                 RfbLimits::fitsDesktopGeometry(3840, 2160) &&
                 RfbLimits::fitsDesktopGeometry(11520, 2160) && // three 4K monitors side by side
                 RfbLimits::fitsDesktopGeometry(0, 0),
                 "geometry: real desktops are accepted");
  counters.check(RfbLimits::fitsDesktopGeometry(limit, limit) &&
                 RfbLimits::fitsDesktopGeometry(limit, 1) &&
                 RfbLimits::fitsDesktopGeometry(1, limit),
                 "geometry: exactly the ceiling is accepted");
  counters.check(!RfbLimits::fitsDesktopGeometry(limit + 1, limit) &&
                 !RfbLimits::fitsDesktopGeometry(limit, limit + 1) &&
                 !RfbLimits::fitsDesktopGeometry(limit + 1, 1) &&
                 !RfbLimits::fitsDesktopGeometry(65535, 65535),
                 "geometry: one pixel above the ceiling is refused");
  counters.check(!RfbLimits::fitsDesktopGeometry(-1, 100) &&
                 !RfbLimits::fitsDesktopGeometry(100, -1),
                 "geometry: a negative size is refused");

  // The check this replaced let a desktop of about 2 GiB through, and a resize
  // allocates a second buffer of the same size.
  counters.check(!RfbLimits::fitsDesktopGeometry(23168, 23168),
                 "geometry: the ceiling refuses the 23168x23168 desktop");
}

//----------------------------------------------------------------------------
// Pixel format (C-04).

PixelFormat makeFormat(unsigned bpp,
                       unsigned redMax, unsigned redShift,
                       unsigned greenMax, unsigned greenShift,
                       unsigned blueMax, unsigned blueShift)
{
  PixelFormat pf;
  pf.bitsPerPixel = static_cast<unsigned short>(bpp);
  pf.redMax = static_cast<unsigned short>(redMax);
  pf.redShift = static_cast<unsigned short>(redShift);
  pf.greenMax = static_cast<unsigned short>(greenMax);
  pf.greenShift = static_cast<unsigned short>(greenShift);
  pf.blueMax = static_cast<unsigned short>(blueMax);
  pf.blueShift = static_cast<unsigned short>(blueShift);
  return pf;
}

void testPixelFormat(SecurityTest::Counters &counters)
{
  // The formats that a TightVNC server announces (and that the viewer asks
  // for itself) must stay accepted: a refusal here breaks every connection.
  counters.check(RfbLimits::isUsablePixelFormat(makeFormat(32, 255, 16, 255, 8, 255, 0)),
                 "pixel format: 32 bpp, 8 bits per colour, is accepted");
  counters.check(RfbLimits::isUsablePixelFormat(makeFormat(16, 31, 11, 63, 5, 31, 0)),
                 "pixel format: 16 bpp 5-6-5 is accepted");
  counters.check(RfbLimits::isUsablePixelFormat(makeFormat(8, 7, 0, 7, 3, 3, 6)),
                 "pixel format: 8 bpp 3-3-2 (BGR233) is accepted");
  counters.check(RfbLimits::isUsablePixelFormat(makeFormat(32, 255, 0, 255, 8, 255, 16)),
                 "pixel format: 32 bpp with the colours in the other order is accepted");
  counters.check(RfbLimits::isUsablePixelFormat(makeFormat(16, 31, 10, 31, 5, 31, 0)),
                 "pixel format: 16 bpp 5-5-5 is accepted");

  // Every format that the viewer itself can ask for.
  counters.check(RfbLimits::isUsablePixelFormat(StandardPixelFormatFactory::create32bppPixelFormat()) &&
                 RfbLimits::isUsablePixelFormat(StandardPixelFormatFactory::create16bppPixelFormat()) &&
                 RfbLimits::isUsablePixelFormat(StandardPixelFormatFactory::create8bppPixelFormat()) &&
                 RfbLimits::isUsablePixelFormat(StandardPixelFormatFactory::create6bppPixelFormat()) &&
                 RfbLimits::isUsablePixelFormat(StandardPixelFormatFactory::create3bppPixelFormat()),
                 "pixel format: every standard format of the viewer is accepted");

  // The largest shift that still lies inside the pixel.
  counters.check(RfbLimits::isUsablePixelFormat(makeFormat(8, 1, 7, 1, 6, 1, 5)) &&
                 RfbLimits::isUsablePixelFormat(makeFormat(16, 1, 15, 1, 14, 1, 13)) &&
                 RfbLimits::isUsablePixelFormat(makeFormat(32, 1, 31, 1, 30, 1, 29)),
                 "pixel format: a shift of bitsPerPixel - 1 is accepted");

  // The crash of the audit: 8 bpp with redMax = 0 divided by zero in DibSection.
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(8, 0, 0, 7, 3, 3, 6)),
                 "pixel format: 8 bpp with redMax = 0 is refused");
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(8, 7, 0, 0, 3, 3, 6)),
                 "pixel format: greenMax = 0 is refused");
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(8, 7, 0, 7, 3, 0, 6)),
                 "pixel format: blueMax = 0 is refused");
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(32, 0, 0, 0, 0, 0, 0)) &&
                 !RfbLimits::isUsablePixelFormat(makeFormat(8, 0, 0, 0, 0, 0, 0)),
                 "pixel format: all maxima zero (palette formats, a zeroed struct) is refused");

  // A shift at or past the width of the pixel.
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(8, 7, 8, 7, 3, 3, 6)),
                 "pixel format: 8 bpp with redShift = 8 is refused");
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(16, 31, 11, 63, 16, 31, 0)),
                 "pixel format: 16 bpp with greenShift = 16 is refused");
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(32, 255, 16, 255, 8, 255, 32)),
                 "pixel format: 32 bpp with blueShift = 32 is refused");
  counters.check(!RfbLimits::isUsablePixelFormat(makeFormat(32, 255, 255, 255, 8, 255, 0)),
                 "pixel format: a shift of 255 is refused");
}

//----------------------------------------------------------------------------
// ZRLE compressed length (B-04).

class TestableZrle : public ZrleDecoder
{
public:
  TestableZrle(LogWriter *log) : ZrleDecoder(log) {}

  // Outcome of readAndInflate on a wire image; *output receives what came out
  // of the inflater.
  Outcome run(const std::string &wire, size_t maximalUnpackedSize,
              size_t *consumed, std::string *output)
  {
    MemoryInputStream stream(wire);
    RfbInputGate gate(&stream);
    Outcome outcome = ACCEPTED;
    output->clear();
    try {
      readAndInflate(&gate, maximalUnpackedSize);
      output->assign(reinterpret_cast<const char *>(m_inflater.getOutput()),
                     m_inflater.getOutputSize());
    } catch (const IOException &) {
      outcome = RAN_DRY;
    } catch (const Exception &) {
      outcome = REFUSED;
    }
    *consumed = stream.position();
    return outcome;
  }
};

// The compressed form of data, as a server sends it: zlib, flushed with
// Z_SYNC_FLUSH and not finished.
std::string deflateSynced(const std::string &data)
{
  z_stream z;
  memset(&z, 0, sizeof(z));
  if (deflateInit(&z, Z_DEFAULT_COMPRESSION) != Z_OK) {
    return std::string();
  }
  std::vector<unsigned char> out(deflateBound(&z, static_cast<uLong>(data.size())) + 64);
  z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
  z.avail_in = static_cast<uInt>(data.size());
  z.next_out = &out[0];
  z.avail_out = static_cast<uInt>(out.size());
  int r = deflate(&z, Z_SYNC_FLUSH);
  size_t produced = out.size() - z.avail_out;
  deflateEnd(&z);
  if (r != Z_OK) {
    return std::string();
  }
  return std::string(reinterpret_cast<char *>(&out[0]), produced);
}

void testZrleLength(SecurityTest::Counters &counters)
{
  LogWriter log(0);
  TestableZrle decoder(&log);

  // One 64x64 tile: 4 + 20481, as ZrleDecoder::getMaxSizeOfRectangle computes.
  const size_t maxUnpacked = 20485;
  const size_t ceiling = RfbLimits::maxZrleCompressedBytes(maxUnpacked);

  // Data that does not compress, so the compressed length stays close to the
  // largest legitimate one.
  std::string noise(20400, '\0');
  UINT32 state = 12345;
  for (size_t i = 0; i < noise.size(); i++) {
    state = state * 1664525u + 1013904223u;
    noise[i] = static_cast<char>(state >> 24);
  }
  std::string compressed = deflateSynced(noise);
  counters.check(compressed.size() > 20000 && compressed.size() < maxUnpacked,
                 "zrle: the test payload is a real zlib stream just under maximalUnpackedSize");

  std::string wire;
  putU32(&wire, static_cast<UINT32>(compressed.size()));
  wire += compressed;
  size_t consumed = 0;
  std::string output;
  Outcome outcome = decoder.run(wire, maxUnpacked, &consumed, &output);
  counters.check(outcome == ACCEPTED && consumed == wire.size() && output == noise,
                 "zrle: a length just under maximalUnpackedSize is still accepted and inflated");

  // An empty payload is not refused by the ceiling.
  std::string emptyWire;
  putU32(&emptyWire, 0);
  outcome = decoder.run(emptyWire, maxUnpacked, &consumed, &output);
  counters.check(outcome == ACCEPTED && consumed == 4,
                 "zrle: a zero length is not refused by the ceiling");

  // One byte above the ceiling, with enough bytes after it to be read.
  std::string over;
  putU32(&over, static_cast<UINT32>(ceiling + 1));
  over.append(ceiling + 1, 'x');
  outcome = decoder.run(over, maxUnpacked, &consumed, &output);
  counters.check(outcome == REFUSED && consumed == 4,
                 "zrle: a length above the ceiling is refused before its payload is read");

  // A length far above maximalUnpackedSize, as an attacker sends it.
  std::string huge;
  putU32(&huge, 0xFFFFFFFFu);
  huge.append(64, 'x');
  outcome = decoder.run(huge, maxUnpacked, &consumed, &output);
  counters.check(outcome == REFUSED && consumed == 4,
                 "zrle: a declared 4 GiB is refused and not allocated");

  // The ceiling is built from the value that the caller passes.
  counters.check(RfbLimits::maxZrleCompressedBytes(maxUnpacked) >= maxUnpacked &&
                 RfbLimits::maxZrleCompressedBytes(10 * maxUnpacked) >
                     RfbLimits::maxZrleCompressedBytes(maxUnpacked),
                 "zrle: the ceiling is at least maximalUnpackedSize and follows the rectangle");
}

//----------------------------------------------------------------------------
// RRE subrectangle count (B-07).

class TestableRre : public RreDecoder
{
public:
  TestableRre(LogWriter *log) : RreDecoder(log) {}

  Outcome run(FrameBuffer *fb, const Rect *rect, const std::string &wire,
              size_t *consumed)
  {
    MemoryInputStream stream(wire);
    RfbInputGate gate(&stream);
    Outcome outcome = ACCEPTED;
    try {
      decode(&gate, fb, rect);
    } catch (const IOException &) {
      outcome = RAN_DRY;
    } catch (const Exception &) {
      outcome = REFUSED;
    }
    *consumed = stream.position();
    return outcome;
  }
};

// An RRE rectangle on the wire for a 32 bpp format: the declared count, the
// background pixel and then subrectsOnWire 1x1 subrectangles of one colour.
std::string rreWire(UINT32 count, UINT32 subrectsOnWire, UINT32 background, UINT32 color)
{
  std::string wire;
  putU32(&wire, count);
  wire.append(reinterpret_cast<const char *>(&background), 4);
  for (UINT32 i = 0; i < subrectsOnWire; i++) {
    wire.append(reinterpret_cast<const char *>(&color), 4);
    putU16(&wire, i % 10); // x
    putU16(&wire, i / 10); // y
    putU16(&wire, 1);
    putU16(&wire, 1);
  }
  return wire;
}

void testRreCount(SecurityTest::Counters &counters)
{
  LogWriter log(0);
  TestableRre decoder(&log);
  FrameBuffer fb;
  Dimension dim(10, 10);
  PixelFormat pf = StandardPixelFormatFactory::create32bppPixelFormat();
  fb.setProperties(&dim, &pf);
  Rect rect(0, 0, 10, 10); // 100 pixels

  size_t consumed = 0;
  // A plausible count: three subrectangles in a 100-pixel rectangle. They are
  // at (0,0), (1,0) and (2,0).
  std::string plausible = rreWire(3, 3, 0x00102030, 0x00AABBCC);
  Outcome outcome = decoder.run(&fb, &rect, plausible, &consumed);
  counters.check(outcome == ACCEPTED && consumed == plausible.size(),
                 "rre: a plausible subrectangle count is accepted and fully read");
  const UINT32 *painted = static_cast<const UINT32 *>(fb.getBufferPtr(1, 0));
  const UINT32 *background = static_cast<const UINT32 *>(fb.getBufferPtr(5, 5));
  counters.check(*painted == 0x00AABBCC && *background == 0x00102030,
                 "rre: the accepted rectangle is painted (subrectangle over background)");

  // The boundary: as many subrectangles as pixels.
  std::string full = rreWire(100, 100, 0, 0x00112233);
  outcome = decoder.run(&fb, &rect, full, &consumed);
  counters.check(outcome == ACCEPTED && consumed == full.size(),
                 "rre: a count equal to the pixel area is accepted");

  // One more than the area, and the wire carries no subrectangles: a loop that
  // ran would hit the end of the stream (RAN_DRY), so a refusal with only the
  // count consumed proves that the check came first.
  std::string tooMany = rreWire(101, 0, 0, 0);
  outcome = decoder.run(&fb, &rect, tooMany, &consumed);
  counters.check(outcome == REFUSED && consumed == 4,
                 "rre: a count above the pixel area is refused before anything else is read");

  std::string huge = rreWire(0xFFFFFFFFu, 0, 0, 0);
  outcome = decoder.run(&fb, &rect, huge, &consumed);
  counters.check(outcome == REFUSED && consumed == 4,
                 "rre: a count of 4 billion is refused");

  // A one-pixel rectangle holds at most one subrectangle.
  Rect one(2, 2, 3, 3);
  std::string two = rreWire(2, 2, 0, 1);
  outcome = decoder.run(&fb, &one, two, &consumed);
  counters.check(outcome == REFUSED && consumed == 4,
                 "rre: two subrectangles in a one-pixel rectangle are refused");
}

} // namespace

void runRfbLimitsTests(int *passed, int *failed)
{
  SecurityTest::Counters counters(passed, failed);
  testReadUtf8(counters);
  testReadInBlocks(counters);
  testClipboardLength(counters);
  testTightCapabilityCount(counters);
  testDesktopGeometry(counters);
  testPixelFormat(counters);
  testZrleLength(counters);
  testRreCount(counters);
}
