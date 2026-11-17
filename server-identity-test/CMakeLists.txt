# server-identity-test: console test program (no PCH, no .vcxproj counterpart).
#
# The CMake tree covers the viewer only; the server still builds through
# tightvnc2017.sln / MSBuild (tools/build.ps1 -Project tvnserver). The server is
# NOT ported here. Instead, the few server translation units that define the
# constants under test are compiled straight into this test executable:
#
#   tvnserver-app/NamingDefs.cpp      the identity constants themselves. It only
#                                     defines constants (plus two
#                                     RegisterWindowMessage calls at static
#                                     initialization), and its header pulls in
#                                     util/CommonHeader.h and nothing else, so
#                                     nothing of the server is dragged along.
#   server-config-lib/ServerConfig.cpp
#                                     the default ports and switches. Its
#                                     constructor needs the constructors of its
#                                     members, hence the five other
#                                     server-config-lib sources below and no
#                                     more: ServerConfig.cpp includes only
#                                     ServerConfig.h, win-system/Environment.h,
#                                     file-lib/File.h and io-lib/DataCopy.h, and
#                                     references neither Configurator nor
#                                     RegistrySecurityAttributes, so the registry
#                                     side of server-config-lib stays out.
#
# Compiling them in (rather than linking a server .lib) keeps the viewer-only
# CMake tree intact and keeps the test honest: it reads the very same sources
# that MSBuild compiles into fsvncserver.exe, in the same repository, so the two
# builds cannot drift apart.
#
# Everything else is an existing viewer CMake target: util, thread, io-lib,
# file-lib, region and win-system are shared libraries of both products and are
# already built by this tree.
add_executable(server-identity-test
  ServerIdentityTest.cpp
  "${FSV_ROOT}/tvnserver-app/NamingDefs.cpp"
  "${FSV_ROOT}/server-config-lib/ServerConfig.cpp"
  "${FSV_ROOT}/server-config-lib/PortMapping.cpp"
  "${FSV_ROOT}/server-config-lib/PortMappingContainer.cpp"
  "${FSV_ROOT}/server-config-lib/PortMappingRect.cpp"
  "${FSV_ROOT}/server-config-lib/IpAccessControl.cpp"
  "${FSV_ROOT}/server-config-lib/IpAccessRule.cpp")

fsv_configure_executable(server-identity-test)
target_include_directories(server-identity-test PRIVATE ${FSV_ROOT})

# desktop/HookInstaller.cpp keeps its DLL name in a private static constant whose
# owning class loads the library in its constructor, so the requirement on that
# name is checked by reading the source text. The path comes from here so the
# test runs from any working directory.
target_compile_definitions(server-identity-test PRIVATE
  "FSV_HOOKINSTALLER_SOURCE=\"${FSV_ROOT}/desktop/HookInstaller.cpp\"")

# Ws2_32: IpAccessRule.cpp (a ServerConfig member) parses addresses with
# inet_addr/inet_ntoa/htonl.
target_link_libraries(server-identity-test PRIVATE
  file-lib
  io-lib
  region
  thread
  util
  win-system
  Ws2_32)

add_test(NAME server-identity-test COMMAND server-identity-test)

# Same requirement, checked on the bytes that actually ship instead of on the
# source: it catches a stale object file or a resource script that the
# source-level test above cannot see. The script only reads files (no registry,
# no service, no process), and it exits 0 with a SKIPPED message when the server
# has not been built, because the server is built by MSBuild and not by this
# tree. It is scoped to the MSBuild output directory of this platform and
# configuration, so a stale binary from another configuration is not scanned.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(FSV_SERVER_BIN_PLATFORM x64)
else()
  set(FSV_SERVER_BIN_PLATFORM Win32)
endif()

add_test(NAME server-identity-strings
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File "${FSV_ROOT}/tools/scan-server-identity.ps1"
          -Path "${FSV_ROOT}/build/bin/${FSV_SERVER_BIN_PLATFORM}/$<CONFIG>")
