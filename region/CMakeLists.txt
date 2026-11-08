# region: static library, mirrors region.vcxproj.
add_library(region STATIC
  RectSerializer.cpp
  Region.cpp
  x11region.c)

fsv_configure_target(region)
target_compile_definitions(region PRIVATE _LIB)
target_include_directories(region PRIVATE ${FSV_ROOT})

# No PCH: 2 small C++ files plus one C file.
