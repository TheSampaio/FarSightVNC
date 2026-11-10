# win-system: static library, mirrors win-system.vcxproj.
add_library(win-system STATIC
  AnonymousPipe.cpp
  AnonymousPipeFactory.cpp
  AutoImpersonator.cpp
  CtrlAltDelSimulator.cpp
  CurrentConsoleProcess.cpp
  DibSection.cpp
  DynamicLibrary.cpp
  EmulatedAnonymousPipeFactory.cpp
  Environment.cpp
  Impersonator.cpp
  InputInjector.cpp
  Keyboard.cpp
  LocalWindowsApplication.cpp
  NamedPipe.cpp
  ParentProcess.cpp
  Pipe.cpp
  PipeClient.cpp
  PipeImpersonatedThread.cpp
  PipeServer.cpp
  Process.cpp
  ProcessHandle.cpp
  Registry.cpp
  RegistryKey.cpp
  SCMClient.cpp
  Screen.cpp
  SecurityAttributes.cpp
  security/SecurityDescriptor.cpp
  security/SecurityIdentifier.cpp
  Service.cpp
  SharedMemory.cpp
  Shell.cpp
  SystemException.cpp
  SystemInformation.cpp
  UipiControl.cpp
  VersionInfo.cpp
  WinClipboard.cpp
  WinCommandLineArgs.cpp
  WinCursor.cpp
  WindowsApplication.cpp
  WindowsDisplays.cpp
  WindowsEvent.cpp
  WindowsEventEx.cpp
  WinHandles.cpp
  WinHooks.cpp
  WinProcessCommandLine.cpp
  WinStaLibrary.cpp
  WinTimeMillis.cpp
  Workstation.cpp
  WsaStartup.cpp
  WTS.cpp)

fsv_configure_target(win-system)
target_compile_definitions(win-system PRIVATE NOMINMAX _LIB)
target_include_directories(win-system PRIVATE ${FSV_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(win-system PUBLIC Wtsapi32 psapi)

# EmulatedAnonymousPipeFactory.cpp defines _CRT_RAND_S, which must come before <stdlib.h>.
fsv_enable_pch(win-system
  SKIP EmulatedAnonymousPipeFactory.cpp)
