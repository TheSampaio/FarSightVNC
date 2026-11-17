# viewer-security-test: console test program, mirrors viewer-security-test.vcxproj (no PCH).
add_executable(viewer-security-test
  SecurityTestMain.cpp
  FileNameRulesTest.cpp
  FileTransferPathTest.cpp
  FileTransferWireTest.cpp
  RfbLimitsTest.cpp
  FrameBufferRaiiTest.cpp)

fsv_configure_executable(viewer-security-test)
target_include_directories(viewer-security-test PRIVATE ${FSV_ROOT})
target_link_libraries(viewer-security-test PRIVATE
  file-lib
  ft-client-lib
  ft-common
  io-lib
  log-server
  log-writer
  network
  region
  rfb
  thread
  util
  viewer-core
  win-system
  libjpeg
  zlib)

add_test(NAME viewer-security-test COMMAND viewer-security-test)
