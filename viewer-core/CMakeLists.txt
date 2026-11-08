# viewer-core: static library, mirrors viewer-core.vcxproj.
add_library(viewer-core STATIC
  AuthHandler.cpp
  CapabilitiesManager.cpp
  CapsContainer.cpp
  CoreEventsAdapter.cpp
  CursorPainter.cpp
  DecoderOfRectangle.cpp
  FbUpdateNotifier.cpp
  FileTransferCapability.cpp
  LastRectDecoder.cpp
  PseudoDecoder.cpp
  RawDecoder.cpp
  RemoteViewerCore.cpp
  RfbClientToServerMessage.cpp
  RfbCutTextEventClientMessage.cpp
  RfbFramebufferUpdateRequestClientMessage.cpp
  RfbKeyEventClientMessage.cpp
  RfbPointerEventClientMessage.cpp
  RfbSetEncodingsClientMessage.cpp
  RfbSetPixelFormatClientMessage.cpp
  TcpConnection.cpp
  InputEventSender.cpp
  UpdateRequestSender.cpp
  VncAuthentication.cpp
  CompressionLevel.cpp
  CopyRectDecoder.cpp
  Decoder.cpp
  DecoderStore.cpp
  DesktopSizeDecoder.cpp
  HexTileDecoder.cpp
  JpegDecompressor.cpp
  JpegQualityLevel.cpp
  PointerPosDecoder.cpp
  RichCursorDecoder.cpp
  RreDecoder.cpp
  TightDecoder.cpp
  VncAuthenticationHandler.cpp
  WatermarksController.cpp
  ZrleDecoder.cpp)

fsv_configure_target(viewer-core)
target_compile_definitions(viewer-core PRIVATE NOMINMAX _LIB)
if(CMAKE_SIZEOF_VOID_P EQUAL 4) # Win32 only, as in viewer-core.vcxproj
  target_compile_definitions(viewer-core PRIVATE DO_NOT_USE_TIGHT_SECURITY_TYPE)
endif()
target_include_directories(viewer-core PRIVATE ${FSV_ROOT})
target_link_libraries(viewer-core PUBLIC libjpeg)

fsv_enable_pch(viewer-core)
