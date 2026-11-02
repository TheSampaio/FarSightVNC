// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a modified version of TightVNC.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Modified for FarSightVNC on 2026-11-02: optional GPU presentation with safe GDI fallback.
//

#include "GpuPresenter.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
// WRL needs Vista-or-newer SDK declarations. Keep this translation-unit
// setting local; Direct3D entry points are still resolved at runtime.
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000
#include <wrl/client.h>
#include <math.h>
#include <vector>

#include "region/Region.h"
#include "thread/AutoLock.h"
#include "thread/LocalMutex.h"

using Microsoft::WRL::ComPtr;

namespace {

typedef HRESULT (WINAPI *CreateDeviceFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                         const D3D_FEATURE_LEVEL *, UINT, UINT,
                                         ID3D11Device **, D3D_FEATURE_LEVEL *,
                                         ID3D11DeviceContext **);
typedef HRESULT (WINAPI *CreateFactoryFn)(D2D1_FACTORY_TYPE, REFIID,
                                          const D2D1_FACTORY_OPTIONS *, void **);

// After this many failures in a row the presenter stops trying.
const int MAX_FAILURES = 3;

// Texture formats of the 32 bit layouts that map to the DIB byte order.
DXGI_FORMAT formatFor(const GpuPresenter::Frame &f)
{
  if (f.bitsPerPixel != 32 || f.bigEndian ||
      f.redMax != 255 || f.greenMax != 255 || f.blueMax != 255 || f.greenShift != 8) {
    return DXGI_FORMAT_UNKNOWN;
  }
  if (f.redShift == 16 && f.blueShift == 0) {
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  }
  if (f.redShift == 0 && f.blueShift == 16) {
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  return DXGI_FORMAT_UNKNOWN;
}

} // namespace

struct GpuPresenter::Impl
{
  explicit Impl(LogWriter *logWriter)
  : log(logWriter), d3d11Dll(0), d2d1Dll(0), createDevice(0), createFactory(0),
    librariesTried(false), failures(0), disabled(false), maxBitmap(0),
    swapWindow(0), swapW(0), swapH(0),
    bitmapBits(0), bitmapW(0), bitmapH(0), bitmapFormat(DXGI_FORMAT_UNKNOWN),
    fullUpload(true)
  {
  }

  ~Impl()
  {
    releaseDevice();
    if (d2d1Dll != 0) {
      FreeLibrary(d2d1Dll);
    }
    if (d3d11Dll != 0) {
      FreeLibrary(d3d11Dll);
    }
  }

  bool loadLibraries()
  {
    if (librariesTried) {
      return createDevice != 0 && createFactory != 0;
    }
    librariesTried = true;
    d3d11Dll = LoadLibraryEx(_T("d3d11.dll"), 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    d2d1Dll = LoadLibraryEx(_T("d2d1.dll"), 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (d3d11Dll != 0) {
      createDevice = reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d11Dll, "D3D11CreateDevice"));
    }
    if (d2d1Dll != 0) {
      createFactory = reinterpret_cast<CreateFactoryFn>(GetProcAddress(d2d1Dll, "D2D1CreateFactory"));
    }
    if (createDevice == 0 || createFactory == 0) {
      log->info(_T("GpuPresenter: Direct3D 11 / Direct2D are not available, using GDI"));
      return false;
    }
    return true;
  }

  // Every failure frees all GPU objects (so GDI can paint into the window) and
  // is counted; a repeated failure disables the GPU path for the session.
  void fail(const TCHAR *what, HRESULT hr)
  {
    log->error(_T("GpuPresenter: %s failed (0x%08X), falling back to GDI"), what,
               static_cast<unsigned>(hr));
    releaseDevice();
    if (++failures >= MAX_FAILURES) {
      disabled = true;
      log->error(_T("GpuPresenter: disabled for this session after %d failures"), failures);
    }
  }

  bool ensureDevice()
  {
    if (ctx) {
      return true;
    }
    if (!loadLibraries()) {
      disabled = true;
      return false;
    }

    const D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_10_0;
    HRESULT hr = createDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              levels, 4, D3D11_SDK_VERSION, &d3d, &obtained, 0);
    if (hr == E_INVALIDARG) {
      // Older runtimes do not know feature level 11.1.
      hr = createDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                        levels + 1, 3, D3D11_SDK_VERSION, &d3d, &obtained, 0);
    }
    if (FAILED(hr)) {
      fail(_T("creating the Direct3D 11 device"), hr);
      return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3d.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(hr)) {
      hr = dxgiDevice->GetAdapter(&adapter);
    }
    if (SUCCEEDED(hr)) {
      hr = adapter->GetParent(__uuidof(IDXGIFactory2),
                              reinterpret_cast<void **>(dxgiFactory.ReleaseAndGetAddressOf()));
    }
    if (FAILED(hr)) {
      fail(_T("getting the DXGI factory"), hr);
      return false;
    }

    DXGI_ADAPTER_DESC desc;
    if (SUCCEEDED(adapter->GetDesc(&desc))) {
      // "Microsoft Basic Render Driver": a CPU rasterizer, slower than GDI.
      if (desc.VendorId == 0x1414 && desc.DeviceId == 0x8C) {
        log->info(_T("GpuPresenter: only the software adapter is available, using GDI"));
        releaseDevice();
        disabled = true;
        return false;
      }
      log->info(_T("GpuPresenter: adapter \"%ls\", feature level 0x%X"), desc.Description,
                static_cast<unsigned>(obtained));
    }

    D2D1_FACTORY_OPTIONS options = {};
    options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    hr = createFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                       reinterpret_cast<void **>(d2dFactory.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr)) {
      hr = d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
    }
    if (SUCCEEDED(hr)) {
      hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);
    }
    if (FAILED(hr)) {
      fail(_T("creating the Direct2D device"), hr);
      return false;
    }
    // Work in pixels, whatever the DPI of the monitor is.
    ctx->SetDpi(96.0f, 96.0f);
    maxBitmap = ctx->GetMaximumBitmapSize();
    markAllDirty();
    return true;
  }

  bool createTarget()
  {
    ComPtr<IDXGISurface> surface;
    HRESULT hr = swap->GetBuffer(0, __uuidof(IDXGISurface),
                                 reinterpret_cast<void **>(surface.GetAddressOf()));
    if (SUCCEEDED(hr)) {
      D2D1_BITMAP_PROPERTIES1 props = {};
      props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
      props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
      props.dpiX = 96.0f;
      props.dpiY = 96.0f;
      props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
      hr = ctx->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target);
    }
    if (FAILED(hr)) {
      fail(_T("binding the swap chain to Direct2D"), hr);
      return false;
    }
    ctx->SetTarget(target.Get());
    return true;
  }

  bool ensureSwapChain(HWND hwnd, UINT w, UINT h)
  {
    if (swap && swapWindow == hwnd) {
      if (swapW == w && swapH == h) {
        return true;
      }
      ctx->SetTarget(0);
      target.Reset();
      HRESULT hr = swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
      if (FAILED(hr)) {
        fail(_T("resizing the swap chain"), hr);
        return false;
      }
      swapW = w;
      swapH = h;
      return createTarget();
    }

    releaseSwapChain();
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // This HWND also supports GDI. Flip-model presentation permanently
    // prevents GDI painting into the HWND, even after releasing its swap
    // chain, so use the bit-blt model to preserve the automatic fallback.
    desc.BufferCount = 1;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, 0, 0, &swap);
    if (FAILED(hr)) {
      fail(_T("creating the swap chain"), hr);
      return false;
    }
    // The viewer handles Alt+Enter and window changes itself.
    dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    swapWindow = hwnd;
    swapW = w;
    swapH = h;
    return createTarget();
  }

  bool ensureBitmap(const GpuPresenter::Frame &f, DXGI_FORMAT format)
  {
    if (fbBitmap && bitmapBits == f.bits && bitmapW == f.width && bitmapH == f.height &&
        bitmapFormat == format) {
      return true;
    }
    fbBitmap.Reset();
    D2D1_BITMAP_PROPERTIES1 props = {};
    props.pixelFormat.format = format;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
    HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(f.width, f.height), 0, 0, &props, &fbBitmap);
    if (FAILED(hr)) {
      fail(_T("creating the frame buffer bitmap"), hr);
      return false;
    }
    bitmapBits = f.bits;
    bitmapW = f.width;
    bitmapH = f.height;
    bitmapFormat = format;
    markAllDirty();
    return true;
  }

  bool upload(const GpuPresenter::Frame &f)
  {
    bool full = false;
    std::vector<Rect> rects;
    {
      AutoLock al(&dirtyLock);
      if (fullUpload) {
        full = true;
        fullUpload = false;
      } else {
        dirty.getRectVector(&rects);
        // More than half of the frame changed: one big copy is cheaper.
        size_t area = 0;
        for (size_t i = 0; i < rects.size(); i++) {
          area += rects[i].area();
        }
        if (area * 2 > static_cast<size_t>(f.width) * f.height) {
          full = true;
        }
      }
      dirty.clear();
    }

    HRESULT hr = S_OK;
    const unsigned char *base = static_cast<const unsigned char *>(f.bits);
    if (full) {
      hr = fbBitmap->CopyFromMemory(0, base, f.stride);
    } else {
      for (size_t i = 0; SUCCEEDED(hr) && i < rects.size(); i++) {
        Rect r = rects[i];
        if (r.left < 0) r.left = 0;
        if (r.top < 0) r.top = 0;
        if (r.right > f.width) r.right = f.width;
        if (r.bottom > f.height) r.bottom = f.height;
        if (r.getWidth() <= 0 || r.getHeight() <= 0) {
          continue;
        }
        D2D1_RECT_U dest = D2D1::RectU(r.left, r.top, r.right, r.bottom);
        hr = fbBitmap->CopyFromMemory(&dest, base + static_cast<size_t>(r.top) * f.stride + r.left * 4,
                                      f.stride);
      }
    }
    if (FAILED(hr)) {
      fail(_T("uploading the frame buffer"), hr);
      return false;
    }
    return true;
  }

  void markAllDirty()
  {
    AutoLock al(&dirtyLock);
    fullUpload = true;
    dirty.clear();
  }

  void releaseSwapChain()
  {
    if (ctx) {
      ctx->SetTarget(0);
    }
    target.Reset();
    swap.Reset();
    swapWindow = 0;
    swapW = swapH = 0;
  }

  void releaseDevice()
  {
    releaseSwapChain();
    fbBitmap.Reset();
    bitmapBits = 0;
    bitmapW = bitmapH = 0;
    bitmapFormat = DXGI_FORMAT_UNKNOWN;
    ctx.Reset();
    d2dDevice.Reset();
    d2dFactory.Reset();
    dxgiFactory.Reset();
    d3d.Reset();
    markAllDirty();
  }

  D2D1_INTERPOLATION_MODE chooseInterpolation(const Rect &src, const Rect &dst) const
  {
    double sx = static_cast<double>(dst.getWidth()) / src.getWidth();
    double sy = static_cast<double>(dst.getHeight()) / src.getHeight();
    bool integerZoom = sx >= 1.0 && sy >= 1.0 &&
                       fabs(sx - floor(sx + 0.5)) < 1e-6 && fabs(sy - floor(sy + 0.5)) < 1e-6;
    if (integerZoom) {
      // 100 percent and exact integer zoom: no filtering, exact pixels.
      return D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    }
    return sx < 1.0 || sy < 1.0 ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
                                : D2D1_INTERPOLATION_MODE_LINEAR;
  }

  LogWriter *log;
  HMODULE d3d11Dll;
  HMODULE d2d1Dll;
  CreateDeviceFn createDevice;
  CreateFactoryFn createFactory;
  bool librariesTried;
  int failures;
  bool disabled;
  UINT maxBitmap;

  ComPtr<ID3D11Device> d3d;
  ComPtr<IDXGIFactory2> dxgiFactory;
  ComPtr<ID2D1Factory1> d2dFactory;
  ComPtr<ID2D1Device> d2dDevice;
  ComPtr<ID2D1DeviceContext> ctx;

  ComPtr<IDXGISwapChain1> swap;
  ComPtr<ID2D1Bitmap1> target;
  HWND swapWindow;
  UINT swapW, swapH;

  ComPtr<ID2D1Bitmap1> fbBitmap;
  const void *bitmapBits;
  int bitmapW, bitmapH;
  DXGI_FORMAT bitmapFormat;

  LocalMutex dirtyLock;
  Region dirty;
  bool fullUpload;
};

GpuPresenter::GpuPresenter(LogWriter *logWriter)
: m_impl(new Impl(logWriter))
{
}

GpuPresenter::~GpuPresenter()
{
  delete m_impl;
}

void GpuPresenter::markDirty(const Rect *rect)
{
  AutoLock al(&m_impl->dirtyLock);
  if (!m_impl->fullUpload) {
    m_impl->dirty.addRect(rect);
  }
}

void GpuPresenter::invalidateAll()
{
  m_impl->markAllDirty();
}

bool GpuPresenter::isDisabled() const
{
  return m_impl->disabled;
}

void GpuPresenter::shutdown()
{
  m_impl->releaseDevice();
}

bool GpuPresenter::present(HWND hwnd, const Frame &frame, const Rect &src, const Rect &dst)
{
  Impl &d = *m_impl;
  if (d.disabled) {
    return false;
  }

  DXGI_FORMAT format = formatFor(frame);
  RECT client;
  if (format == DXGI_FORMAT_UNKNOWN || frame.bits == 0 || frame.width <= 0 || frame.height <= 0 ||
      src.getWidth() <= 0 || src.getHeight() <= 0 || dst.getWidth() <= 0 || dst.getHeight() <= 0 ||
      src.left < 0 || src.top < 0 || src.right > frame.width || src.bottom > frame.height ||
      !GetClientRect(hwnd, &client)) {
    // Not a failure: this frame (8/16 bit colour, unusual layout, empty) is
    // left to GDI, which needs the window free of the swap chain.
    d.releaseSwapChain();
    return false;
  }
  UINT w = static_cast<UINT>(client.right - client.left);
  UINT h = static_cast<UINT>(client.bottom - client.top);
  if (w == 0 || h == 0) {
    return true; // nothing visible
  }

  if (!d.ensureDevice()) {
    return false;
  }
  if (w > d.maxBitmap || h > d.maxBitmap ||
      static_cast<UINT>(frame.width) > d.maxBitmap || static_cast<UINT>(frame.height) > d.maxBitmap) {
    d.releaseSwapChain();
    return false;
  }
  if (!d.ensureSwapChain(hwnd, w, h) || !d.ensureBitmap(frame, format) || !d.upload(frame)) {
    return false;
  }

  D2D1_RECT_F destF = D2D1::RectF(static_cast<float>(dst.left), static_cast<float>(dst.top),
                                  static_cast<float>(dst.right), static_cast<float>(dst.bottom));
  D2D1_RECT_F srcF = D2D1::RectF(static_cast<float>(src.left), static_cast<float>(src.top),
                                 static_cast<float>(src.right), static_cast<float>(src.bottom));
  D2D1_COLOR_F black = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);

  d.ctx->BeginDraw();
  d.ctx->Clear(&black);
  d.ctx->DrawBitmap(d.fbBitmap.Get(), &destF, 1.0f, d.chooseInterpolation(src, dst), &srcF, 0);
  HRESULT hr = d.ctx->EndDraw();
  if (FAILED(hr)) {
    // D2DERR_RECREATE_TARGET (device lost) among others: rebuild everything.
    d.fail(_T("drawing"), hr);
    return false;
  }

  hr = d.swap->Present(0, 0);
  if (FAILED(hr)) {
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
      d.log->error(_T("GpuPresenter: device removed, reason 0x%08X"),
                   static_cast<unsigned>(d.d3d->GetDeviceRemovedReason()));
    }
    d.fail(_T("presenting"), hr);
    return false;
  }
  d.failures = 0;
  return true;
}
