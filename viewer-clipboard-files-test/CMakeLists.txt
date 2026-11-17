# viewer-clipboard-files-test: console test program, mirrors viewer-clipboard-files-test.vcxproj (no PCH).
add_executable(viewer-clipboard-files-test
  ClipboardFilesTest.cpp
  ClipboardFilesSessionTest.cpp)

fsv_configure_executable(viewer-clipboard-files-test)
target_include_directories(viewer-clipboard-files-test PRIVATE ${FSV_ROOT})
target_link_libraries(viewer-clipboard-files-test PRIVATE
  file-lib
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

add_test(NAME viewer-clipboard-files-test COMMAND viewer-clipboard-files-test)
