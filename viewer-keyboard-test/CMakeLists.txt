# viewer-keyboard-test: console test program, mirrors viewer-keyboard-test.vcxproj (no PCH: a single translation unit).
add_executable(viewer-keyboard-test
  KeyboardTest.cpp)

fsv_configure_executable(viewer-keyboard-test)
target_include_directories(viewer-keyboard-test PRIVATE ${FSV_ROOT})
target_link_libraries(viewer-keyboard-test PRIVATE
  file-lib
  io-lib
  log-server
  log-writer
  rfb
  thread
  util
  win-system)

add_test(NAME viewer-keyboard-test COMMAND viewer-keyboard-test)
