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
// The ownership of the frame buffer, of the decoders and of the DIB section:
// the pointer that must never be left on freed memory, the two inherited fixes
// of FrameBuffer that must not come back, and the pixel format check of the DIB
// section.
//

#include "viewer-security-test/SecurityTestHarness.h"

#include "rfb/FrameBuffer.h"
#include "rfb/PixelFormat.h"
#include "region/Dimension.h"
#include "region/Rect.h"
#include "util/Exception.h"
#include "viewer-core/DecoderStore.h"
#include "viewer-core/TightDecoder.h"
#include "win-system/DibSection.h"

#include <atomic>
#include <memory>
#include <new>
#include <string.h>
#include <thread>
#include <vector>

namespace {

using SecurityTest::Counters;

const UINT8 GUARD_BYTE = 0xA5;
const UINT8 FILL_BYTE = 0x11;
const size_t GUARD_SIZE = 64;

PixelFormat makePixelFormat(unsigned short bpp,
                            unsigned short redMax, unsigned short redShift,
                            unsigned short greenMax, unsigned short greenShift,
                            unsigned short blueMax, unsigned short blueShift)
{
  PixelFormat pf;
  pf.bitsPerPixel = bpp;
  pf.colorDepth = (bpp == 32) ? 24 : bpp;
  pf.redMax = redMax;
  pf.greenMax = greenMax;
  pf.blueMax = blueMax;
  pf.redShift = redShift;
  pf.greenShift = greenShift;
  pf.blueShift = blueShift;
  pf.initBigEndianByNative();
  return pf;
}

PixelFormat pixelFormat32()
{
  return makePixelFormat(32, 255, 16, 255, 8, 255, 0);
}

// Storage of "areaSize" bytes with a guard pattern before and after, handed to
// a FrameBuffer through setBuffer() so that a write outside the pixel area lands
// on the guard and is seen.
class GuardedStorage
{
public:
  explicit GuardedStorage(size_t areaSize)
  : m_areaSize(areaSize), m_bytes(areaSize + 2 * GUARD_SIZE, GUARD_BYTE)
  {
    memset(area(), FILL_BYTE, m_areaSize);
  }

  UINT8 *area() { return &m_bytes[GUARD_SIZE]; }

  bool guardsIntact() const
  {
    for (size_t i = 0; i < GUARD_SIZE; i++) {
      if (m_bytes[i] != GUARD_BYTE ||
          m_bytes[GUARD_SIZE + m_areaSize + i] != GUARD_BYTE) {
        return false;
      }
    }
    return true;
  }

  bool areaIsUntouched() const
  {
    for (size_t i = 0; i < m_areaSize; i++) {
      if (m_bytes[GUARD_SIZE + i] != FILL_BYTE) {
        return false;
      }
    }
    return true;
  }

private:
  size_t m_areaSize;
  std::vector<UINT8> m_bytes;
};

UINT32 pixelAt(GuardedStorage &storage, int index)
{
  UINT32 value;
  memcpy(&value, storage.area() + index * 4, 4);
  return value;
}

//----------------------------------------------------------------------------
// FrameBuffer::fillRect: the inherited fix, an empty clipped rectangle writes
// nothing.

void testFillRectEmptyRectangles(Counters &counters)
{
  const int W = 4;
  const int H = 3;
  GuardedStorage storage(W * H * 4);
  FrameBuffer fb;
  Dimension dim(W, H);
  PixelFormat pf = pixelFormat32();
  fb.setPropertiesWithoutResize(&dim, &pf);
  fb.setBuffer(storage.area());

  struct Case { Rect rect; const char *name; };
  const Case cases[] = {
    { Rect(0, H, W, H), "fillRect: a zero-height rectangle on the bottom edge writes nothing" },
    { Rect(W, 0, W, H), "fillRect: a zero-width rectangle on the right edge writes nothing" },
    { Rect(0, 0, W, 0), "fillRect: a zero-height rectangle on the top edge writes nothing" },
    { Rect(2, 1, 2, 2), "fillRect: a zero-width rectangle inside writes nothing" },
    { Rect(3, 2, 1, 1), "fillRect: an inverted rectangle writes nothing" },
    { Rect(10, 10, 12, 12), "fillRect: a rectangle outside the buffer writes nothing" },
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    fb.fillRect(&cases[i].rect, 0xAABBCCDD);
    counters.check(storage.guardsIntact() && storage.areaIsUntouched(), cases[i].name);
  }

  // A normal rectangle must still be painted, and only inside itself.
  Rect inner(1, 1, 3, 3);
  fb.fillRect(&inner, 0xAABBCCDD);
  bool exact = true;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      bool inside = x >= 1 && x < 3 && y >= 1 && y < 3;
      UINT32 value = pixelAt(storage, y * W + x);
      if (inside ? value != 0xAABBCCDD : value != 0x11111111) {
        exact = false;
      }
    }
  }
  counters.check(exact, "fillRect: a normal rectangle fills exactly its pixels");
  counters.check(storage.guardsIntact(), "fillRect: a normal rectangle stays inside the buffer");

  // The rectangle that covers the whole buffer, bottom row included.
  Rect whole(0, 0, W, H);
  fb.fillRect(&whole, 0x01020304);
  bool allFilled = true;
  for (int i = 0; i < W * H; i++) {
    if (pixelAt(storage, i) != 0x01020304) {
      allFilled = false;
    }
  }
  counters.check(allFilled && storage.guardsIntact(),
                 "fillRect: the whole buffer is filled and nothing past it");

  fb.setBuffer(0); // the storage is not owned by the frame buffer
}

//----------------------------------------------------------------------------
// FrameBuffer::setColor: the inherited fix, a zero pixel size returns instead
// of looping for ever.

struct SetColorJob
{
  SetColorJob() : done(false), storage(48) {}
  std::atomic<bool> done;
  GuardedStorage storage;
  FrameBuffer fb;
};

// Runs setColor() on a frame buffer with a buffer but no pixel format. The call
// runs on its own thread so that a regression (an endless loop) fails the test
// after a timeout instead of hanging the whole run. A stuck thread is left
// behind and the process ends when main() returns.
bool setColorReturnsWithZeroPixelSize()
{
  std::shared_ptr<SetColorJob> job = std::make_shared<SetColorJob>();
  Dimension dim(4, 3);
  PixelFormat noFormat; // bitsPerPixel == 0: pixel size 0
  job->fb.setPropertiesWithoutResize(&dim, &noFormat);
  job->fb.setBuffer(job->storage.area());
  std::thread worker([job]() {
    job->fb.setColor(1, 2, 3);
    job->fb.setBuffer(0);
    job->done = true;
  });
  worker.detach();
  for (int i = 0; i < 500 && !job->done; i++) {
    Sleep(10);
  }
  return job->done && job->storage.guardsIntact() && job->storage.areaIsUntouched();
}

void testSetColor(Counters &counters)
{
  counters.check(setColorReturnsWithZeroPixelSize(),
                 "setColor: a zero pixel size returns and writes nothing");

  // Without storage: nothing to fill, nothing to crash on.
  {
    FrameBuffer noStorage;
    Dimension dim(4, 3);
    PixelFormat pf = pixelFormat32();
    noStorage.setPropertiesWithoutResize(&dim, &pf);
    noStorage.setColor(1, 2, 3);
    counters.check(noStorage.getBuffer() == 0, "setColor: a frame buffer without storage returns");
  }
  {
    GuardedStorage storage(48);
    FrameBuffer empty;
    Dimension dim(0, 0);
    PixelFormat pf = pixelFormat32();
    empty.setPropertiesWithoutResize(&dim, &pf);
    empty.setBuffer(storage.area());
    empty.setColor(1, 2, 3);
    counters.check(storage.guardsIntact() && storage.areaIsUntouched(),
                   "setColor: an empty dimension writes nothing");
    empty.setBuffer(0);
  }

  // The normal case still fills every pixel and nothing else.
  {
    GuardedStorage storage(4 * 3 * 4);
    FrameBuffer fb;
    Dimension dim(4, 3);
    PixelFormat pf = pixelFormat32();
    fb.setPropertiesWithoutResize(&dim, &pf);
    fb.setBuffer(storage.area());
    fb.setColor(255, 255, 255);
    bool filled = true;
    for (int i = 0; i < 12; i++) {
      if (pixelAt(storage, i) != 0x00FFFFFF) {
        filled = false;
      }
    }
    counters.check(filled && storage.guardsIntact(),
                   "setColor: a normal frame buffer is filled entirely and not past its end");
    fb.setBuffer(0);
  }
}

//----------------------------------------------------------------------------
// FrameBuffer::resizeBuffer.

bool lastPixelIsWritable(FrameBuffer &fb, int w, int h)
{
  fb.setColor(255, 255, 255);
  const UINT8 *last = static_cast<const UINT8 *>(fb.getBuffer()) + (w * h - 1) * 4;
  UINT32 value;
  memcpy(&value, last, 4);
  return value == 0x00FFFFFF;
}

void testResizeBuffer(Counters &counters)
{
  PixelFormat pf = pixelFormat32();
  {
    FrameBuffer fb;
    Dimension big(64, 48);
    bool ok = fb.setProperties(&big, &pf);
    counters.check(ok && fb.getBuffer() != 0 && fb.getBufferSize() == 64 * 48 * 4,
                   "resizeBuffer: the first allocation has the size of the geometry");
    counters.check(lastPixelIsWritable(fb, 64, 48),
                   "resizeBuffer: the last pixel of the first allocation is usable");

    ok = fb.setDimension(&big);
    counters.check(ok && fb.getBuffer() != 0 && fb.getBufferSize() == 64 * 48 * 4,
                   "resizeBuffer: a resize to the same size leaves a consistent object");
    counters.check(lastPixelIsWritable(fb, 64, 48),
                   "resizeBuffer: the last pixel after a resize to the same size is usable");

    Dimension smallDim(5, 7);
    ok = fb.setDimension(&smallDim);
    counters.check(ok && fb.getBuffer() != 0 && fb.getBufferSize() == 5 * 7 * 4 &&
                   fb.getDimension().cmpDim(&smallDim),
                   "resizeBuffer: a resize to a smaller size leaves a consistent object");
    counters.check(lastPixelIsWritable(fb, 5, 7),
                   "resizeBuffer: the last pixel after a resize to a smaller size is usable");
  } // destroyed here: a bad pointer would be freed twice

  {
    FrameBuffer fb;
    const int sizes[][2] = { {1, 1}, {640, 480}, {3, 2}, {0, 0}, {320, 200}, {1, 1}, {17, 13} };
    bool allOk = true;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
      Dimension dim(sizes[i][0], sizes[i][1]);
      allOk = fb.setProperties(&dim, &pf) && fb.getBuffer() != 0 &&
              fb.getBufferSize() == dim.area() * 4 && allOk;
      if (dim.area() > 0) {
        allOk = lastPixelIsWritable(fb, dim.width, dim.height) && allOk;
      }
    }
    counters.check(allOk, "resizeBuffer: a sequence of resizes keeps the object consistent");
  } // destroyed here
}

// A geometry whose byte size does not fit getBufferSize(): 32768 * 16384 * 4
// bytes is 2^31, which becomes a negative int, so new[] throws. It fails the
// same way on every machine, with no dependence on the free memory.
void testResizeBufferAllocationFailure(Counters &counters)
{
  PixelFormat pf = pixelFormat32();
  Dimension unallocatable(32768, 16384);

  // Failure after a good allocation: this is the delete-then-new path.
  {
    FrameBuffer fb;
    Dimension smallDim(8, 8);
    fb.setProperties(&smallDim, &pf);
    bool threw = false;
    try {
      fb.setDimension(&unallocatable);
    } catch (const std::bad_alloc &) {
      threw = true;
    }
    counters.check(threw, "resizeBuffer failure: the allocation failure reaches the caller");
    counters.check(fb.getBuffer() == 0,
                   "resizeBuffer failure: the freed buffer is no longer referenced");
    counters.check(fb.getBufferSize() == 0 && fb.getDimension().isEmpty(),
                   "resizeBuffer failure: the object describes an empty frame buffer");
    // Operations on the failed object must not touch the missing buffer.
    Rect r(0, 0, 8, 8);
    fb.fillRect(&r, 0xFFFFFFFF);
    fb.setColor(1, 2, 3);
    counters.check(true, "resizeBuffer failure: fillRect and setColor on the failed object return");

    // The object can be used again after a good resize.
    bool ok = fb.setDimension(&smallDim);
    counters.check(ok && fb.getBuffer() != 0 && lastPixelIsWritable(fb, 8, 8),
                   "resizeBuffer failure: the object recovers with a valid geometry");
  } // destroyed here

  // Failure on the first allocation (no buffer to free yet).
  {
    FrameBuffer fb;
    bool threw = false;
    try {
      fb.setProperties(&unallocatable, &pf);
    } catch (const std::bad_alloc &) {
      threw = true;
    }
    counters.check(threw && fb.getBuffer() == 0,
                   "resizeBuffer failure: the first allocation can fail and leaves no buffer");
  } // destroyed here
}

//----------------------------------------------------------------------------
// DecoderStore: a duplicate decoder is destroyed with the right delete.

class CountingDecoder : public Decoder
{
public:
  CountingDecoder(int code, int *destroyed) : Decoder(0), m_destroyed(destroyed)
  {
    m_encoding = code;
  }
  virtual ~CountingDecoder() { (*m_destroyed)++; }

private:
  int *m_destroyed;
};

void testDecoderStore(Counters &counters)
{
  LogWriter logWriter(0);
  int firstDestroyed = 0;
  int duplicateDestroyed = 0;
  int otherDestroyed = 0;
  {
    DecoderStore store(&logWriter);
    Decoder *first = new CountingDecoder(EncodingDefs::TIGHT, &firstDestroyed);
    Decoder *duplicate = new CountingDecoder(EncodingDefs::TIGHT, &duplicateDestroyed);
    Decoder *other = new CountingDecoder(EncodingDefs::RRE, &otherDestroyed);

    counters.check(store.addDecoder(first, 1), "DecoderStore: the first decoder is added");
    counters.check(!store.addDecoder(duplicate, 2),
                   "DecoderStore: a second decoder with the same code is refused");
    counters.check(duplicateDestroyed == 1,
                   "DecoderStore: the refused duplicate is destroyed exactly once");
    counters.check(firstDestroyed == 0 && store.getDecoder(EncodingDefs::TIGHT) == first,
                   "DecoderStore: the registered decoder is kept");
    counters.check(store.addDecoder(other, 1),
                   "DecoderStore: a decoder with another code is added");

    counters.check(store.removeDecoder(EncodingDefs::RRE) && otherDestroyed == 1,
                   "DecoderStore: a removed decoder is destroyed exactly once");
    counters.check(store.getDecoder(EncodingDefs::RRE) == 0 &&
                   !store.removeDecoder(EncodingDefs::RRE),
                   "DecoderStore: a removed decoder is gone and cannot be removed twice");
  } // the store destroys what is left
  counters.check(firstDestroyed == 1 && duplicateDestroyed == 1 && otherDestroyed == 1,
                 "DecoderStore: every decoder is destroyed exactly once when the store dies");
}

// The Tight decoder owns its four inflaters: its construction (which builds and
// resets them) and its destruction, alone and through the store, must be clean.
// The reset on a compression-control byte needs a rectangle stream and is not
// reachable from here.
void testTightDecoder(Counters &counters)
{
  LogWriter logWriter(0);
  {
    TightDecoder alone(&logWriter);
    counters.check(alone.getCode() == EncodingDefs::TIGHT,
                   "TightDecoder: it is constructed with the Tight code");
  }
  {
    DecoderStore store(&logWriter);
    Decoder *tight = new TightDecoder(&logWriter);
    counters.check(store.addDecoder(tight, 1) && store.getDecoder(EncodingDefs::TIGHT) == tight,
                   "TightDecoder: it is registered in the store");
    counters.check(!store.addDecoder(new TightDecoder(&logWriter), 1),
                   "TightDecoder: a duplicate Tight decoder is refused and destroyed");
  } // the store destroys its Tight decoder
  counters.check(true, "TightDecoder: the store and its Tight decoder are destroyed cleanly");
}

//----------------------------------------------------------------------------
// DibSection: a pixel format it cannot represent is refused, the ordinary
// ones are accepted.

bool dibSectionIsRefused(const PixelFormat &pf)
{
  Dimension dim(8, 4);
  try {
    DibSection dib(&pf, &dim, 0);
  } catch (const Exception &) {
    return true;
  }
  return false;
}

bool dibSectionIsAccepted(const PixelFormat &pf)
{
  Dimension dim(8, 4);
  try {
    DibSection dib(&pf, &dim, 0);
    return dib.getBuffer() != 0;
  } catch (const Exception &) {
    return false;
  }
}

void testDibSection(Counters &counters)
{
  counters.check(dibSectionIsRefused(makePixelFormat(8, 0, 0, 7, 3, 3, 6)),
                 "DibSection: 8 bpp with redMax == 0 is refused");
  counters.check(dibSectionIsRefused(makePixelFormat(8, 7, 0, 0, 3, 3, 6)),
                 "DibSection: 8 bpp with greenMax == 0 is refused");
  counters.check(dibSectionIsRefused(makePixelFormat(8, 7, 0, 7, 3, 0, 6)),
                 "DibSection: 8 bpp with blueMax == 0 is refused");
  counters.check(dibSectionIsRefused(makePixelFormat(8, 0, 0, 0, 0, 0, 0)),
                 "DibSection: 8 bpp with every maximum zero is refused");

  counters.check(dibSectionIsAccepted(makePixelFormat(8, 7, 0, 7, 3, 3, 6)),
                 "DibSection: the ordinary 8 bpp format (BGR233) is accepted");
  counters.check(dibSectionIsAccepted(makePixelFormat(8, 7, 5, 7, 2, 3, 0)),
                 "DibSection: the ordinary 8 bpp format (RGB332) is accepted");
  counters.check(dibSectionIsAccepted(makePixelFormat(16, 31, 11, 63, 5, 31, 0)),
                 "DibSection: the ordinary 16 bpp format is accepted");
  counters.check(dibSectionIsAccepted(makePixelFormat(32, 255, 16, 255, 8, 255, 0)),
                 "DibSection: the ordinary 32 bpp format is accepted");
}

} // namespace

void runFrameBufferRaiiTests(int *passed, int *failed)
{
  Counters counters(passed, failed);
  testFillRectEmptyRectangles(counters);
  testSetColor(counters);
  testResizeBuffer(counters);
  testResizeBufferAllocationFailure(counters);
  testDecoderStore(counters);
  testTightDecoder(counters);
  testDibSection(counters);
}
