# io-lib: static library, mirrors io-lib.vcxproj.
add_library(io-lib STATIC
  BufferedInputStream.cpp
  BufferedOutputStream.cpp
  ByteArrayInputStream.cpp
  ByteArrayOutputStream.cpp
  Channel.cpp
  DataCopy.cpp
  DataInputStream.cpp
  DataOutputStream.cpp
  InputStream.cpp
  IOException.cpp
  OutputStream.cpp)

fsv_configure_target(io-lib)
target_compile_definitions(io-lib PRIVATE NOMINMAX _LIB)
target_include_directories(io-lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})

fsv_enable_pch(io-lib)
