# viewer-security-test: console test program, mirrors viewer-security-test.vcxproj (no PCH).
add_executable(viewer-security-test
  SecurityTestMain.cpp
  FileNameRulesTest.cpp)

fsv_configure_executable(viewer-security-test)
target_include_directories(viewer-security-test PRIVATE ${FSV_ROOT})
target_link_libraries(viewer-security-test PRIVATE
  util)

add_test(NAME viewer-security-test COMMAND viewer-security-test)
