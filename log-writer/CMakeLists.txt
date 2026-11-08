# log-writer: static library, mirrors log-writer.vcxproj.
add_library(log-writer STATIC
  FileAccount.cpp
  FileLogger.cpp
  LogDump.cpp
  LogWriter.cpp
  ProfileLogger.cpp)

fsv_configure_target(log-writer)
target_compile_definitions(log-writer PRIVATE _LIB)
target_include_directories(log-writer PRIVATE ${FSV_ROOT})

fsv_enable_pch(log-writer)
