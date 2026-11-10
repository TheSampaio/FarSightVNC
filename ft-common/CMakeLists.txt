# ft-common: static library, mirrors ft-common.vcxproj.
add_library(ft-common STATIC
  FileInfo.cpp
  FileTransferException.cpp
  FolderListener.cpp
  FTMessage.cpp
  OperationNotSupportedException.cpp
  WinFilePath.cpp)

fsv_configure_target(ft-common)
target_compile_definitions(ft-common PRIVATE _LIB)
target_include_directories(ft-common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})

# FTMessage.cpp includes no SDK header.
fsv_enable_pch(ft-common
  SKIP FTMessage.cpp)
