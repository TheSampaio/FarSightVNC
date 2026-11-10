# config-lib: static library, mirrors config-lib.vcxproj.
add_library(config-lib STATIC
  ConnectionHistory.cpp
  IniFileSettingsManager.cpp
  RegistrySettingsManager.cpp
  SettingsManager.cpp)

fsv_configure_target(config-lib)
target_compile_definitions(config-lib PRIVATE _LIB)
target_include_directories(config-lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})

fsv_enable_pch(config-lib)
