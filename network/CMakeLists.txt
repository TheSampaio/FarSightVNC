# network: static library, mirrors network.vcxproj.
add_library(network STATIC
  socket/SocketAddressIPv4.cpp
  socket/SocketException.cpp
  socket/SocketIPv4.cpp
  socket/SocketStream.cpp
  socket/WindowsSocket.cpp
  RfbInputGate.cpp
  RfbOutputGate.cpp
  TcpClientThread.cpp
  TcpServer.cpp)

fsv_configure_target(network)
target_compile_definitions(network PRIVATE _LIB)
target_include_directories(network PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})
target_link_libraries(network PUBLIC Ws2_32)

fsv_enable_pch(network)
