# log-server: static library, mirrors log-server.vcxproj.
add_library(log-server STATIC
  ClientLogger.cpp
  ConnectionTimer.cpp
  ListenLogServer.cpp
  LogConn.cpp
  LogLevelSender.cpp
  LogServer.cpp
  SecurityPipeClient.cpp
  SecurityPipeServer.cpp)

fsv_configure_target(log-server)
target_compile_definitions(log-server PRIVATE _LIB)
target_include_directories(log-server PRIVATE ${FSV_ROOT})

# Only the tests use it. SecurityPipeServer.cpp defines _CRT_RAND_S before <stdlib.h>.
fsv_enable_pch(log-server
  SKIP SecurityPipeServer.cpp)
