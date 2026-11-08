# viewer-favorites-test: console test program, mirrors viewer-favorites-test.vcxproj (no PCH: a single translation unit).
add_executable(viewer-favorites-test
  FavoritesTest.cpp)

fsv_configure_executable(viewer-favorites-test)
target_include_directories(viewer-favorites-test PRIVATE ${FSV_ROOT})
target_link_libraries(viewer-favorites-test PRIVATE
  client-config-lib
  log-writer
  thread
  util
  win-system)

add_test(NAME viewer-favorites-test COMMAND viewer-favorites-test)
