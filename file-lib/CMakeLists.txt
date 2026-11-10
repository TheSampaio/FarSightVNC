# file-lib: static library, mirrors file-lib.vcxproj.
add_library(file-lib STATIC
  EOFException.cpp
  File.cpp
  FileNotFoundException.cpp
  WinFile.cpp
  WinFileChannel.cpp)

fsv_configure_target(file-lib)
target_compile_definitions(file-lib PRIVATE _LIB)
target_include_directories(file-lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})

fsv_enable_pch(file-lib)
