# Helpers shared by the per-project CMakeLists.txt files.

# Settings every .vcxproj of the viewer has in common (per configuration):
#   Unicode character set, /W3, /Zc:inline (MSBuild's default), /Gy and whole
#   program optimization (/GL, /LTCG) in Release, _DEBUG/NDEBUG. Everything else
#   (defines, includes, system libraries) is declared by each target, mirroring
#   its own .vcxproj.
function(fsv_configure_target target)
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>)
  target_compile_options(${target} PRIVATE
    /W3
    /Zc:inline
    $<$<CONFIG:Release>:/Gy>)
  set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endfunction()

# Linker settings of the executables (Release: /DEBUG /OPT:REF /OPT:ICF, as in
# the .vcxproj files; Debug already links with /DEBUG).
function(fsv_configure_executable target)
  fsv_configure_target(${target})
  target_compile_definitions(${target} PRIVATE _WINDOWS)
  target_link_options(${target} PRIVATE
    $<$<CONFIG:Release>:/DEBUG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>)
endfunction()

# Precompiled header for the C++ sources of a target (never for C files).
#
# The precompiled header (cmake/fsv_pch.h) contains util/winhdr.h and the STL
# headers the code uses. Only targets whose translation units all reach
# util/winhdr.h before any other SDK header may use it, because winhdr.h fixes
# WINVER/_WIN32_WINNT first; sources that differ (they include no SDK header at
# all, or include <windows.h> with a different WINVER, or define a macro that
# must come before <stdlib.h>) are listed in SKIP and compile without PCH.
#
#   fsv_enable_pch(<target> [SKIP <source>...])
#
# Every target has its own .pch. REUSE_FROM was tried and dropped: the targets
# that reused a .pch shared its PDB, and the x64 Release link of the executables
# then printed hundreds of LNK4020 ("type record ... is corrupted") warnings.
function(fsv_enable_pch target)
  if(NOT FSV_USE_PCH)
    return()
  endif()
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SKIP")
  target_precompile_headers(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${FSV_ROOT}/cmake/fsv_pch.h>")
  foreach(source IN LISTS ARG_SKIP)
    set_source_files_properties(${source} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  endforeach()
endfunction()

# Settings of the updater targets (updater-lib, farsight-updater, updater-test):
# they target Windows 7 (_WIN32_WINNT 0x0601), never include util/winhdr.h or any
# project header outside updater-lib, build without PCH, and compile at /W4 /WX.
function(fsv_configure_updater_target target)
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0601 WINVER=0x0601 NTDDI_VERSION=0x06010000
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>)
  target_compile_options(${target} PRIVATE
    /W4 /WX /Zc:inline
    $<$<CONFIG:Release>:/Gy>)
  set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endfunction()

# Linker settings of the updater executables (same as fsv_configure_executable).
function(fsv_configure_updater_executable target)
  fsv_configure_updater_target(${target})
  target_link_options(${target} PRIVATE
    $<$<CONFIG:Release>:/DEBUG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>)
endfunction()
