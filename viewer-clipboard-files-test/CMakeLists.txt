# viewer-clipboard-files-test: console test program, mirrors viewer-clipboard-files-test.vcxproj (no PCH: a single translation unit).
add_executable(viewer-clipboard-files-test
  ClipboardFilesTest.cpp)

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
  win-system)

add_test(NAME viewer-clipboard-files-test COMMAND viewer-clipboard-files-test)
