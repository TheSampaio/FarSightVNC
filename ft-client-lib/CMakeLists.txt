# ft-client-lib: static library, mirrors ft-client-lib.vcxproj.
add_library(ft-client-lib STATIC
  CopyOperation.cpp
  DownloadOperation.cpp
  FileInfoList.cpp
  FileTransferCore.cpp
  FileTransferEventAdapter.cpp
  FileTransferEventHandler.cpp
  FileTransferInterface.cpp
  FileTransferMessageProcessor.cpp
  FileTransferOperation.cpp
  FileTransferReplyBuffer.cpp
  FileTransferRequestSender.cpp
  LocalFilesDeleteOperation.cpp
  OperationEventListener.cpp
  OperationNotPermittedException.cpp
  OperationSupport.cpp
  RemoteFileListOperation.cpp
  RemoteFileRenameOperation.cpp
  RemoteFilesDeleteOperation.cpp
  RemoteFolderCreateOperation.cpp
  UploadOperation.cpp)

fsv_configure_target(ft-client-lib)
target_compile_definitions(ft-client-lib PRIVATE _LIB __WIN32__ _WINSTATIC)
target_include_directories(ft-client-lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})

# OperationSupport.cpp includes no SDK header.
fsv_enable_pch(ft-client-lib
  SKIP OperationSupport.cpp)
