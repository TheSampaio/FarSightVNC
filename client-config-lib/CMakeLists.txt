# client-config-lib: static library, mirrors client-config-lib.vcxproj.
add_library(client-config-lib STATIC
  ConnectionConfig.cpp
  ConnectionFavorites.cpp
  ConnectionConfigSM.cpp
  ViewerConfig.cpp
  ViewerSettingsManager.cpp)

fsv_configure_target(client-config-lib)
target_compile_definitions(client-config-lib PRIVATE _LIB)
target_include_directories(client-config-lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})

fsv_enable_pch(client-config-lib)
