// Copyright (C) 2026 FarSightVNC contributors.
// SPDX-License-Identifier: GPL-2.0-or-later
// Modified for FarSightVNC on 2026-11-02: native GPU presentation and GDI fallback regression test.
// Uses its own window only; no network, registry writes or input injection.
#include "tvnviewer/GpuPresenter.h"
#include <d3d11.h>
#include <stdio.h>
#include <vector>


namespace {
int passed = 0, failed = 0;
void check(bool value, const char *label) {
  printf("%s: %s\n", value ? "PASS" : "FAIL", label);
  value ? ++passed : ++failed;
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_PAINT) {
    PAINTSTRUCT ps;
    BeginPaint(window, &ps);
    EndPaint(window, &ps);
    return 0;
  }
  return DefWindowProc(window, message, w, l);
}
void pump() {
  MSG message;
  while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }
}
// Read only pixels inside the test's own visible, topmost client area.
bool displayedPixel(HWND window, int x, int y, COLORREF expected) {
  POINT point = { x, y };
  ClientToScreen(window, &point);
  for (int attempt = 0; attempt < 20; ++attempt) {
    pump();
    Sleep(20);
    HDC screen = GetDC(0);
    COLORREF actual = GetPixel(screen, point.x, point.y);
    ReleaseDC(0, screen);
    if (actual == expected) return true;
  }
  return false;
}
bool gdiFallback(HWND window) {
  RECT area;
  GetClientRect(window, &area);
  HDC dc = GetDC(window);
  HBRUSH brush = CreateSolidBrush(RGB(25, 80, 155));
  bool filled = FillRect(dc, &area, brush) != 0;
  GdiFlush();
  COLORREF actual = GetPixel(dc, 12, 12);
  DeleteObject(brush);
  ReleaseDC(window, dc);
  return filled && actual == RGB(25, 80, 155) &&
    displayedPixel(window, 12, 12, RGB(25, 80, 155));
}
bool hardwareAvailable() {
  HMODULE library = LoadLibraryExW(L"d3d11.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!library) return false;
  typedef HRESULT (WINAPI *CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
    UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
    D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
  CreateDevice create = reinterpret_cast<CreateDevice>(GetProcAddress(library, "D3D11CreateDevice"));
  ID3D11Device *device = 0;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
  HRESULT hr = create ? create(0, D3D_DRIVER_TYPE_HARDWARE, 0,
    D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION,
    &device, 0, 0) : E_NOINTERFACE;
  if (device) device->Release();
  FreeLibrary(library);
  if (FAILED(hr)) printf("Hardware device unavailable: 0x%08lX\n", static_cast<unsigned long>(hr));
  return SUCCEEDED(hr);
}
void exercise(HWND window) {
  LogWriter log(0);
  GpuPresenter gpu(&log);
  const int width = 68, height = 36; // storage includes padding outside 65x33
  std::vector<unsigned> pixels(width * height, 0x0000FFFF);
  for (int y = 0; y < 33; ++y)
    for (int x = 0; x < 65; ++x) pixels[y * width + x] = 0x00FF0000;
  GpuPresenter::Frame frame = {};
  frame.bits = &pixels[0]; frame.width = width; frame.height = height;
  frame.stride = width * 4; frame.bitsPerPixel = 32;
  frame.redShift = 16; frame.greenShift = 8; frame.blueShift = 0;
  frame.redMax = frame.greenMax = frame.blueMax = 255;
  Rect source(0, 0, 65, 33), destination(24, 20, 284, 152);
  bool shown = gpu.present(window, frame, source, destination);
  check(shown, "initial GPU presentation on available hardware");
  if (!shown) return;
  check(displayedPixel(window, 100, 70, RGB(255, 0, 0)), "32-bit BGRA pixels");
  check(displayedPixel(window, 282, 150, RGB(255, 0, 0)), "source excludes aligned DIB padding");
  check(displayedPixel(window, 4, 4, RGB(0, 0, 0)), "letterbox background is black");

  Rect dirty(16, 8, 32, 16);
  for (int y = dirty.top; y < dirty.bottom; ++y)
    for (int x = dirty.left; x < dirty.right; ++x) pixels[y * width + x] = 0x0000FF00;
  gpu.markDirty(&dirty);
  check(gpu.present(window, frame, source, destination), "partial dirty upload");
  check(displayedPixel(window, 110, 65, RGB(0, 255, 0)), "updated rectangle visible");
  check(displayedPixel(window, 50, 110, RGB(255, 0, 0)), "unchanged rectangle retained");

  Rect cropped(16, 8, 32, 16), scaled(16, 16, 144, 80);
  check(gpu.present(window, frame, cropped, scaled), "cropped source and scaled destination");
  check(displayedPixel(window, 80, 45, RGB(0, 255, 0)), "cropped source content");
  SetWindowPos(window, HWND_TOPMOST, 40, 40, 420, 280, SWP_NOACTIVATE);
  pump();
  check(gpu.present(window, frame, source, destination), "swap chain resize");
  check(displayedPixel(window, 110, 65, RGB(0, 255, 0)), "content after resize");

  GpuPresenter::Frame unsupported = frame;
  unsupported.bitsPerPixel = 16;
  check(!gpu.present(window, unsupported, source, destination), "16-bit format requests GDI fallback");
  check(gdiFallback(window), "GDI FillRect/GetPixel after successful GPU presentation");
  check(gpu.present(window, frame, source, destination), "GPU can resume after format fallback");
  check(displayedPixel(window, 50, 110, RGB(255, 0, 0)), "content after format fallback");
  gpu.shutdown();
  check(gdiFallback(window), "GDI after explicit shutdown");
  check(gpu.present(window, frame, source, destination), "GPU recreates after shutdown");
  check(displayedPixel(window, 110, 65, RGB(0, 255, 0)), "full upload after recreation");

  GpuPresenter::Frame invalid = frame;
  invalid.bits = 0;
  check(!gpu.present(window, invalid, source, destination), "null framebuffer rejected");
  check(gdiFallback(window), "GDI remains usable after invalid frame");
  gpu.shutdown();
}
}
int main() {
  typedef BOOL (WINAPI *SetDpiAware)();
  SetDpiAware setDpi = reinterpret_cast<SetDpiAware>(
    GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware"));
  if (setDpi) setDpi();
  if (!hardwareAvailable()) {
    puts("SKIP: D3D11 hardware unavailable; GPU/fallback transitions NOT verified.");
    return 0;
  }
  HINSTANCE instance = GetModuleHandle(0);
  WNDCLASSW cls = {};
  cls.lpfnWndProc = windowProc;
  cls.hInstance = instance;
  cls.lpszClassName = L"FarSightVNC.GpuPresenter.Test";
  if (!RegisterClassW(&cls)) { puts("FAIL: RegisterClass"); return 1; }
  HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    cls.lpszClassName, L"FarSightVNC GPU regression test", WS_POPUP | WS_VISIBLE,
    40, 40, 360, 240, 0, 0, instance, 0);
  if (!window) {
    UnregisterClassW(cls.lpszClassName, instance);
    puts("FAIL: CreateWindow");
    return 1;
  }
  pump();
  try { exercise(window); }
  catch (...) { check(false, "unexpected exception"); }
  DestroyWindow(window);
  UnregisterClassW(cls.lpszClassName, instance);
  printf("GPU presenter: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
