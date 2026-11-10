# updater-test: console test program of updater-lib, mirrors updater-test.vcxproj
# (no PCH). Exit code 0 = all checks passed.
add_executable(updater-test
  main.cpp
  TestKeys.cpp
  TestVersion.cpp
  TestCrypto.cpp
  TestFsUtil.cpp
  TestJson.cpp
  TestReleases.cpp
  TestManifest.cpp
  TestZip.cpp
  TestUpdateService.cpp
  TestInstaller.cpp
  TestHttp.cpp)

fsv_configure_updater_executable(updater-test)
target_compile_definitions(updater-test PRIVATE _CONSOLE)
target_include_directories(updater-test PRIVATE ${FSV_ROOT})
target_link_libraries(updater-test PRIVATE updater-lib zlib)

add_test(NAME updater-test COMMAND updater-test)
