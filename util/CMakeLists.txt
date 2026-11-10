# util: static library, mirrors util.vcxproj.
add_library(util STATIC
  AnsiStringStorage.cpp
  BrokenHandleException.cpp
  Command.cpp
  CommandLine.cpp
  CommandLineArgs.cpp
  CommandLineFormatException.cpp
  CommandLineFormatHelp.cpp
  DateTime.cpp
  Deflater.cpp
  DemandTimer.cpp
  DesCrypt.cpp
  Exception.cpp
  GetCPUtime.cpp
  Inflater.cpp
  KeyContainer.cpp
  Keymap.cpp
  MacroCommand.cpp
  md5.cpp
  MemUsage.cpp
  ResourceLoader.cpp
  StringParser.cpp
  StringStorage.cpp
  StringTable.cpp
  Unicode.cpp
  UnicodeStringStorage.cpp
  Utf8StringStorage.cpp
  VncPassCrypt.cpp
  ZLibBase.cpp
  ZlibException.cpp)

fsv_configure_target(util)
target_compile_definitions(util PRIVATE NOMINMAX _LIB)
target_include_directories(util PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})
target_link_libraries(util PUBLIC version)

# GetCPUtime.cpp includes <Windows.h> without winhdr.h; the others include no SDK header.
fsv_enable_pch(util
  SKIP Command.cpp GetCPUtime.cpp MacroCommand.cpp md5.cpp Unicode.cpp ZLibBase.cpp)
