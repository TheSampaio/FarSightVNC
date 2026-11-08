# viewer-input-queue-test: console test program, mirrors viewer-input-queue-test.vcxproj (no PCH: a single translation unit).
add_executable(viewer-input-queue-test
  InputQueueTest.cpp)

fsv_configure_executable(viewer-input-queue-test)
target_include_directories(viewer-input-queue-test PRIVATE ${FSV_ROOT})
target_link_libraries(viewer-input-queue-test PRIVATE
  file-lib
  io-lib
  log-server
  log-writer
  network
  region
  viewer-core
  rfb
  thread
  util
  win-system)

add_test(NAME viewer-input-queue-test COMMAND viewer-input-queue-test)
