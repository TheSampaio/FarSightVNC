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
// Modified for FarSightVNC on 2026-11-02: optional GPU presentation with safe GDI fallback.
//

#ifndef _GPU_PRESENTER_H_
#define _GPU_PRESENTER_H_

#include "util/CommonHeader.h"

#include "log-writer/LogWriter.h"
#include "region/Rect.h"

// Presents the remote frame buffer with Direct3D 11 / DXGI / Direct2D.
//
// The frame buffer stays where it always was: the CPU DIB section that the
// decoders fill. This class uploads the changed parts of the DIB to a GPU
// bitmap and lets the GPU scale it into a bit-blt swap chain of the window,
// replacing the CPU-bound StretchBlt of the GDI path.
//
// The GDI path is the fallback. present() returns false whenever the GPU path
// cannot be used (no hardware device, unsupported pixel format, device lost,
// too large for the device, ...). In that case the swap chain has been
// released, so the window can be painted with GDI right away. After repeated
// failures the presenter disables itself for the rest of the session.
//
// Threading: present() and shutdown() are called by the window (UI) thread
// only; markDirty() may be called from any thread. The Direct3D/Direct2D
// libraries are loaded at run time, so the viewer still starts where they are
// missing.
class GpuPresenter
{
public:
  explicit GpuPresenter(LogWriter *logWriter);
  ~GpuPresenter();

  // Description of the DIB section that holds the frame buffer.
  struct Frame
  {
    const void *bits;       // first byte of the top row
    int width;              // pixels per row actually stored (aligned)
    int height;
    int stride;             // bytes per row
    unsigned bitsPerPixel;
    bool bigEndian;
    unsigned redShift, greenShift, blueShift;
    unsigned redMax, greenMax, blueMax;
  };

  // Tells that a rectangle of the DIB changed (thread safe).
  void markDirty(const Rect *rect);

  // Tells that the whole DIB changed or was replaced (thread safe).
  void invalidateAll();

  // Draws the part 'src' of the frame buffer into the 'dst' rectangle of the
  // client area of hwnd and clears the rest of the client area to black.
  // Returns true if the frame was presented.
  bool present(HWND hwnd, const Frame &frame, const Rect &src, const Rect &dst);

  // Frees every Direct3D object (also the swap chain of the window).
  void shutdown();

  // True after the presenter gave up for the rest of the session.
  bool isDisabled() const;

private:
  GpuPresenter(const GpuPresenter &);
  GpuPresenter &operator=(const GpuPresenter &);

  struct Impl;
  Impl *m_impl;
};

#endif
