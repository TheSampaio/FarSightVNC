# rfb: static library, mirrors rfb.vcxproj.
add_library(rfb STATIC
  AuthDefs.cpp
  CursorShape.cpp
  FrameBuffer.cpp
  HostPath.cpp
  MsgDefs.cpp
  PixelFormat.cpp
  RfbKeySym.cpp
  StandardPixelFormatFactory.cpp
  TunnelDefs.cpp
  VendorDefs.cpp
  EncodingDefs.cpp
  PixelConverter.cpp)

fsv_configure_target(rfb)
target_compile_definitions(rfb PRIVATE _LIB)
target_include_directories(rfb PRIVATE ${FSV_ROOT})

# No PCH: 8 of 12 sources include no SDK header (a PCH would add windows.h to them).
