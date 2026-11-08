# gui: static library, mirrors gui.vcxproj.
add_library(gui STATIC
  BalloonTip.cpp
  BaseDialog.cpp
  BaseWindow.cpp
  DibFrameBuffer.cpp
  drawing/Bitmap.cpp
  drawing/BitmapGraphics.cpp
  drawing/Brush.cpp
  CheckBox.cpp
  ComboBox.cpp
  CommonControlsEx.cpp
  Control.cpp
  drawing/DeviceContext.cpp
  FilteredTextBox.cpp
  drawing/Graphics.cpp
  drawing/Icon.cpp
  ImagedButton.cpp
  ListBox.cpp
  ListView.cpp
  Menu.cpp
  MessageWindow.cpp
  NotifyIcon.cpp
  NotifyIconWindow.cpp
  drawing/Pen.cpp
  PaintWindow.cpp
  ProgressBar.cpp
  drawing/SolidBrush.cpp
  ScrollBar.cpp
  SpinControl.cpp
  StringFilter.cpp
  Tab.cpp
  TabControl.cpp
  TextBox.cpp
  ThemeLib.cpp
  DpiUtils.cpp
  ToolBar.cpp
  Tooltip.cpp
  Trackbar.cpp
  WindowFinder.cpp
  WindowProcHolder.cpp)

fsv_configure_target(gui)
target_compile_definitions(gui PRIVATE _LIB _CRT_SECURE_NO_WARNINGS)
target_include_directories(gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${FSV_ROOT})

# StringFilter.cpp includes no SDK header.
fsv_enable_pch(gui
  SKIP StringFilter.cpp)
