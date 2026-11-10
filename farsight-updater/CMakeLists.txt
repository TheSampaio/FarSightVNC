# farsight-updater: fsvncupdater.exe (Windows subsystem), mirrors farsight-updater.vcxproj.
# The manifest (asInvoker, Common Controls 6, per-monitor DPI) is merged by mt.exe.
add_executable(farsight-updater WIN32
  main.cpp
  farsight-updater.rc
  farsight-updater.manifest)

set_target_properties(farsight-updater PROPERTIES OUTPUT_NAME fsvncupdater)
fsv_configure_updater_executable(farsight-updater)
target_compile_definitions(farsight-updater PRIVATE _WINDOWS)
target_include_directories(farsight-updater PRIVATE ${FSV_ROOT})
target_link_libraries(farsight-updater PRIVATE updater-lib zlib)
# DLL planting defense: only System32 is searched for dependent DLLs.
target_link_options(farsight-updater PRIVATE /DEPENDENTLOADFLAG:0x800)
