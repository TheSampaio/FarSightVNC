# updater-lib: static library of the auto-updater, mirrors updater-lib.vcxproj.
# No PCH: the sources must not include util/winhdr.h (it forces WINVER 0x0501).
add_library(updater-lib STATIC
  Crypto.cpp
  FsUtil.cpp
  HttpClient.cpp
  Installer.cpp
  Json.cpp
  Manifest.cpp
  ProcessScan.cpp
  Releases.cpp
  TrustedKeys.cpp
  UpdateService.cpp
  UpdateTypes.cpp
  ZipReader.cpp)

fsv_configure_updater_target(updater-lib)
target_compile_definitions(updater-lib PRIVATE _LIB)
target_include_directories(updater-lib PRIVATE ${FSV_ROOT})
# zlib is a vendor library; it is linked by the executables that use updater-lib.
target_link_libraries(updater-lib PUBLIC zlib)
