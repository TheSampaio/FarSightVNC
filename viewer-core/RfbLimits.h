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

#ifndef _RFB_LIMITS_H_
#define _RFB_LIMITS_H_

#include "util/inttypes.h"
#include "rfb/PixelFormat.h"

#include <stddef.h>
#include <limits.h>

//
// Every ceiling that the viewer puts on a size or a count that arrives from
// the server, in one place.
//
// The policy:
//   1. A size or a count read from the wire has a named ceiling here.
//   2. The ceiling is checked BEFORE the allocation or the loop that the value
//      would drive.
//   3. A value above the ceiling is a protocol error that ends the session. It
//      is never clamped silently. (One documented exception: the desktop name
//      is only a label, so it is truncated and the rest is drained; see
//      DESKTOP_NAME_MAX_BYTES.)
//   4. A payload that may legitimately be large is read in bounded blocks
//      (DataInputStream::readFullyInBlocks), so memory grows with the bytes
//      that really arrived and not with the number the peer declared.
//
// Compatibility with TightVNC servers outranks every number here: each ceiling
// is far above anything a real TightVNC server sends (the reasoning is next to
// each constant), so a legitimate connection never meets it.
//
namespace RfbLimits
{
  // Reason string of a failed authentication (RFB 3.8, UTF-8 text). A real
  // server sends one short sentence, well under 100 bytes. The caller owns the
  // limit, not DataInputStream::readUTF8: that function is generic and shared
  // with local pipes, the file transfer and the server, which have different
  // needs; only the caller knows what this particular string is.
  const UINT32 MAX_AUTH_REASON_BYTES = 64 * 1024;

  // Clipboard text of ServerCutText and ServerCutTextUtf8. Real traffic is a
  // few bytes up to a few hundred KiB; 64 MiB is far more than anyone copies
  // as text and is still safe because the text is read in bounded blocks.
  const UINT32 MAX_CLIPBOARD_BYTES = 64 * 1024 * 1024;

  // Count of tunnel types and of authentication types of the Tight security
  // type. A TightVNC server sends 1 or 2 of each (no tunnel; password
  // authentication); 256 is more than two orders of magnitude above that.
  const UINT32 MAX_TIGHT_CAPABILITIES = 256;

  // Desktop size, per side (DesktopSize and ServerInit). 16384 is the largest
  // texture and bitmap dimension of current hardware and is wider than a row
  // of four 4K monitors (15360). At 4 bytes per pixel one buffer is exactly
  // 1 GiB; the viewer allocates two. The old ceiling allowed about 2 GiB each.
  const UINT32 MAX_DESKTOP_DIMENSION = 16384;
  static_assert(static_cast<UINT64>(MAX_DESKTOP_DIMENSION) * MAX_DESKTOP_DIMENSION * 4 <= INT_MAX,
                "a framebuffer of the largest desktop must fit the signed size used by the buffers");

  // Desktop name of ServerInit. TightVNC sends the machine name, under 64
  // bytes. Only a label: truncated at this size and the rest drained in
  // DISCARD_BLOCK_BYTES blocks, so the stream stays in sync.
  const UINT32 DESKTOP_NAME_MAX_BYTES = 64 * 1024;
  const UINT32 DISCARD_BLOCK_BYTES = 4096;

  // The compressed length of a ZRLE rectangle may not exceed what the
  // inflater itself would accept as output for this rectangle (its worst case
  // plus 1% plus 1024 bytes, see Inflater::inflate). Deflate output is never
  // meaningfully larger than its input (zlib adds a few bytes per 16 KiB), and
  // a real server never sends a rectangle near the worst case that the ceiling
  // assumes (a raw tile is 16385 bytes against 20481 assumed).
  inline size_t maxZrleCompressedBytes(size_t maximalUnpackedSize)
  {
    return maximalUnpackedSize + maximalUnpackedSize / 100 + 1024;
  }

  // A rectangle of N pixels holds at most N non-overlapping subrectangles, and
  // a TightVNC server's RRE encoder never emits more (it falls back to a
  // different encoding long before that).
  inline bool rreCountFitsRect(UINT32 subrectCount, size_t rectArea)
  {
    return subrectCount <= rectArea;
  }

  inline bool clipboardLengthFits(UINT32 length)
  {
    return length <= MAX_CLIPBOARD_BYTES;
  }

  inline bool tightCapabilityCountFits(UINT32 count)
  {
    return count <= MAX_TIGHT_CAPABILITIES;
  }

  // The geometry that the viewer accepts for its framebuffer. Replaces the
  // old ViewerBufferSize::fitsFrameBuffer check in
  // RemoteViewerCore::setFbProperties (which allowed about 2 GiB per buffer).
  inline bool fitsDesktopGeometry(int width, int height)
  {
    return width >= 0 && height >= 0 &&
           static_cast<UINT32>(width) <= MAX_DESKTOP_DIMENSION &&
           static_cast<UINT32>(height) <= MAX_DESKTOP_DIMENSION;
  }

  // A true colour pixel format that the rest of the viewer can use: every
  // maximum is non-zero (a zero one made DibSection divide by zero) and every
  // shift lies inside the pixel (a larger one shifts past its width). A
  // TightVNC server always announces a maximum of at least 1 and shifts below
  // the pixel width.
  inline bool isUsablePixelFormat(const PixelFormat &pf)
  {
    return pf.redMax != 0 && pf.greenMax != 0 && pf.blueMax != 0 &&
           pf.redShift < pf.bitsPerPixel &&
           pf.greenShift < pf.bitsPerPixel &&
           pf.blueShift < pf.bitsPerPixel;
  }
}

#endif
