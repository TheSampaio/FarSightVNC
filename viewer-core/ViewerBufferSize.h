#ifndef VIEWER_BUFFER_SIZE_H
#define VIEWER_BUFFER_SIZE_H

#include "util/inttypes.h"
#include <limits.h>

namespace ViewerBufferSize {

// The ceiling on the desktop geometry the viewer accepts from the server lives
// in RfbLimits::fitsDesktopGeometry, not here. There used to be a
// fitsFrameBuffer helper in this namespace that only kept a framebuffer under
// INT_MAX bytes, which allowed a desktop of about 2 GiB per buffer; it was
// removed rather than left next to the live ceiling, where a future caller
// could reach for the wrong one.

inline bool fitsDecoderBuffer(int width, int height, int bytesPerPixel)
{
  if (width < 0 || height < 0 || bytesPerPixel <= 0) {
    return false;
  }

  return static_cast<UINT64>(width) * height <= INT_MAX / bytesPerPixel;
}

} // namespace ViewerBufferSize

#endif // VIEWER_BUFFER_SIZE_H
