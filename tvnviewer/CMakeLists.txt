# tvnviewer: the FarSightVNC viewer (Windows subsystem), mirrors tvnviewer.vcxproj.
add_executable(tvnviewer WIN32
  AboutDialog.cpp
  AuthenticationDialog.cpp
  BuildTime.cpp
  ConfigurationDialog.cpp
  ConnectionData.cpp
  ConnectionListener.cpp
  ControlTrayIcon.cpp
  DesktopWindow.cpp
  GpuPresenter.cpp
  FileExistDialog.cpp
  FileInfoListView.cpp
  FileRenameDialog.cpp
  FileTransferMainDialog.cpp
  FsWarningDialog.cpp
  FullScreenToolbar.cpp
  ReconnectDialog.cpp
  HelpDialog.cpp
  LoginDialog.cpp
  main.cpp
  NamingDefs.cpp
  NewFolderDialog.cpp
  OptionsDialog.cpp
  ResourceStrings.cpp
  ScaleManager.cpp
  TvnViewer.cpp
  ViewerCmdLine.cpp
  ViewerCollector.cpp
  ViewerInstance.cpp
  ViewerMenu.cpp
  ViewerVncAuthHandler.cpp
  ViewerWindow.cpp
  tvnviewer.rc
  dpi-aware.manifest)

fsv_configure_executable(tvnviewer)
target_include_directories(tvnviewer PRIVATE ${FSV_ROOT})

# The libraries of the ProjectReference items; comctl32 comes from AdditionalDependencies.
target_link_libraries(tvnviewer PRIVATE
  client-config-lib
  config-lib
  file-lib
  ft-client-lib
  ft-common
  gui
  io-lib
  libjpeg
  log-writer
  network
  region
  rfb
  thread
  util
  viewer-core
  win-system
  zlib
  comctl32)

# Common Controls 6 dependency (AdditionalManifestDependencies); dpi-aware.manifest
# above is merged into the embedded manifest by mt.exe (AdditionalManifestFiles).
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(FSV_PROCESSOR_ARCHITECTURE amd64)
else()
  set(FSV_PROCESSOR_ARCHITECTURE X86)
endif()
target_link_options(tvnviewer PRIVATE
  "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='${FSV_PROCESSOR_ARCHITECTURE}' publicKeyToken='6595b64144ccf1df' language='*'")

# BuildTime.cpp includes no SDK header; ReconnectDialog.cpp sets WINVER 0x0600 before
# <windows.h> (util/winhdr.h would fix 0x0501 first). Both compile without PCH.
fsv_enable_pch(tvnviewer
  SKIP BuildTime.cpp ReconnectDialog.cpp)
