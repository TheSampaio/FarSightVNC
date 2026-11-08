# thread: static library, mirrors thread.vcxproj.
add_library(thread STATIC
  AutoLock.cpp
  DesktopSelector.cpp
  GlobalMutex.cpp
  GuiThread.cpp
  LocalMutex.cpp
  Thread.cpp
  ThreadCollector.cpp
  ZombieKiller.cpp)

fsv_configure_target(thread)
target_compile_definitions(thread PRIVATE _LIB)
target_include_directories(thread PRIVATE ${FSV_ROOT})

# AutoLock.cpp includes no SDK header.
fsv_enable_pch(thread
  SKIP AutoLock.cpp)
