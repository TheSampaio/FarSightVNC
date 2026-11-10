# FarSightVNC - experimental CMake build of the viewer (tvnviewer) and its tests.
#
# The official build is still tightvnc2017.sln / MSBuild (tools/build.ps1). This
# tree mirrors the .vcxproj settings of the viewer, the libraries it links
# (including vendor/zlib and vendor/libjpeg) and the three viewer test programs.
# The server projects are not covered yet; see README.md.
#
# Minimum versions:
#   * 3.25 - target_precompile_headers needs 3.16, CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
#            (policy CMP0141) needs 3.25, and CMakePresets.json "version": 6 needs 3.25.
#   * The "Visual Studio 18 2026" generator used by CMakePresets.json only exists in
#     recent CMake releases (4.2 and later); 3.25 is enough for the CMakeLists themselves.
cmake_minimum_required(VERSION 3.25)

project(FarSightVNC LANGUAGES C CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "FarSightVNC only builds with MSVC (Visual Studio generator).")
endif()

option(FSV_USE_PCH "Use precompiled headers (target_precompile_headers)" ON)

# C++20 for every C++ target (cl gets /std:c++20, never c++latest).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT (/MT, /MTd), like every .vcxproj (CMP0091 is NEW since 3.15).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Debug information: /Zi everywhere, except /ZI (Edit and Continue) for Win32
# Debug, as in the .vcxproj files. Release also has /Zi (+ /DEBUG at link time).
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<IF:$<AND:$<CONFIG:Debug>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>>,EditAndContinue,ProgramDatabase>")

# CMake adds /D_WINDOWS to every compile by default; the .vcxproj files only
# define it for the executables (zlib's zconf.h would change behavior with it),
# so it is removed here and added back per executable target.
string(REPLACE "/D_WINDOWS" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
string(REPLACE "/D_WINDOWS" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Executables and static libraries of every target land in <build>/bin/<config>.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(FSV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FsvTargets)

enable_testing()

# Third-party libraries (C).
add_subdirectory(vendor/zlib)
add_subdirectory(vendor/libjpeg)

# Libraries shared by the viewer and the tests.
add_subdirectory(util)
add_subdirectory(io-lib)
add_subdirectory(thread)
add_subdirectory(win-system)
add_subdirectory(network)
add_subdirectory(region)
add_subdirectory(rfb)
add_subdirectory(log-writer)
add_subdirectory(log-server)
add_subdirectory(file-lib)
add_subdirectory(config-lib)
add_subdirectory(client-config-lib)
add_subdirectory(ft-common)
add_subdirectory(ft-client-lib)
add_subdirectory(gui)
add_subdirectory(viewer-core)
add_subdirectory(updater-lib)

# Applications.
add_subdirectory(tvnviewer)
add_subdirectory(viewer-keyboard-test)
add_subdirectory(viewer-input-queue-test)
add_subdirectory(viewer-favorites-test)
add_subdirectory(farsight-updater)
add_subdirectory(updater-test)
