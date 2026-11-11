// Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
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

#include "ChromeRenderer.h"

#include <math.h>

namespace {

// IID of ID2D1DeviceContext5 (d2d1_3.h, Windows 10 1703). util/winhdr.h fixes
// WINVER 0x0501, under which the SDK hides that interface and the DirectWrite
// types it uses, so only its IID is declared here for the run-time check of
// supportsSvgGlyphs(). The icon phase, which calls it, compiles its source
// with a newer WINVER (as ReconnectDialog.cpp does).
const IID IID_D2D1DeviceContext5 =
  { 0x7836d248, 0x68cc, 0x4df6, { 0xb9, 0xe8, 0xde, 0x99, 0x1b, 0xf6, 0x2e, 0xb7 } };

template <class T> void safeRelease(T *&object)
{
  if (object != 0) {
    object->Release();
    object = 0;
  }
}

// Factories shared by the renderers of the process, released at exit.
struct SharedFactories
{
  SharedFactories() : d2d(0), dwrite(0), d2dTried(false), dwriteTried(false) {}
  ~SharedFactories()
  {
    safeRelease(dwrite);
    safeRelease(d2d);
  }
  ID2D1Factory *d2d;
  IDWriteFactory *dwrite;
  bool d2dTried;
  bool dwriteTried;
};

SharedFactories &sharedFactories()
{
  static SharedFactories factories;
  return factories;
}

D2D1_COLOR_F toColor(ThemeColor color)
{
  return D2D1::ColorF(color);
}

}

ChromeRenderer::ChromeRenderer()
: m_ellipsis(0),
  m_hwnd(0),
  m_dpi(96),
  m_fontFamily(kLightTheme.fontFamily),
  m_target(0),
  m_brush(0),
  m_svgSupport(-1)
{
}

ChromeRenderer::~ChromeRenderer()
{
  discardTarget();
  for (size_t i = 0; i < m_formats.size(); i++) {
    safeRelease(m_formats[i].format);
  }
  safeRelease(m_ellipsis);
}

ID2D1Factory *ChromeRenderer::getD2DFactory()
{
  SharedFactories &factories = sharedFactories();
  if (!factories.d2dTried) {
    factories.d2dTried = true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factories.d2d))) {
      factories.d2d = 0;
    }
  }
  return factories.d2d;
}

IDWriteFactory *ChromeRenderer::getDWriteFactory()
{
  SharedFactories &factories = sharedFactories();
  if (!factories.dwriteTried) {
    factories.dwriteTried = true;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(&factories.dwrite)))) {
      factories.dwrite = 0;
    }
  }
  return factories.dwrite;
}

void ChromeRenderer::attach(HWND hwnd, UINT dpi, const wchar_t *fontFamily)
{
  if (hwnd != m_hwnd) {
    discardTarget();
  }
  m_hwnd = hwnd;
  m_fontFamily = fontFamily;
  setDpi(dpi);
}

void ChromeRenderer::setDpi(UINT dpi)
{
  m_dpi = dpi != 0 ? dpi : 96;
  if (m_target != 0) {
    m_target->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
  }
}

UINT ChromeRenderer::getDpi() const
{
  return m_dpi;
}

void ChromeRenderer::resize(UINT width, UINT height)
{
  if (m_target != 0 && FAILED(m_target->Resize(D2D1::SizeU(width, height)))) {
    discardTarget();
  }
}

float ChromeRenderer::pixelsToDips(int pixels) const
{
  return pixels * 96.0f / m_dpi;
}

int ChromeRenderer::dipsToPixels(float dips) const
{
  return static_cast<int>(floorf(dips * m_dpi / 96.0f + 0.5f));
}

bool ChromeRenderer::createTarget()
{
  if (m_target != 0) {
    return true;
  }
  ID2D1Factory *factory = getD2DFactory();
  if (factory == 0 || m_hwnd == 0) {
    return false;
  }
  RECT client;
  if (!GetClientRect(m_hwnd, &client)) {
    return false;
  }
  D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
    D2D1_RENDER_TARGET_TYPE_DEFAULT,
    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
    static_cast<float>(m_dpi), static_cast<float>(m_dpi));
  D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties = D2D1::HwndRenderTargetProperties(
    m_hwnd, D2D1::SizeU(client.right - client.left, client.bottom - client.top));
  if (FAILED(factory->CreateHwndRenderTarget(properties, hwndProperties, &m_target))) {
    m_target = 0;
    return false;
  }
  m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  if (FAILED(m_target->CreateSolidColorBrush(D2D1::ColorF(0), &m_brush))) {
    m_brush = 0;
    discardTarget();
    return false;
  }
  m_svgSupport = -1;
  return true;
}

void ChromeRenderer::discardTarget()
{
  safeRelease(m_brush);
  safeRelease(m_target);
  m_svgSupport = -1;
}

bool ChromeRenderer::beginDraw()
{
  if (!createTarget()) {
    return false;
  }
  m_target->BeginDraw();
  m_target->SetTransform(D2D1::Matrix3x2F::Identity());
  return true;
}

void ChromeRenderer::endDraw()
{
  if (m_target == 0) {
    return;
  }
  HRESULT hr = m_target->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    discardTarget();
    InvalidateRect(m_hwnd, 0, FALSE);
  }
}

ID2D1SolidColorBrush *ChromeRenderer::brush(ThemeColor color)
{
  m_brush->SetColor(toColor(color));
  return m_brush;
}

void ChromeRenderer::clear(ThemeColor color)
{
  m_target->Clear(toColor(color));
}

void ChromeRenderer::fillRect(const D2D1_RECT_F &rect, ThemeColor color)
{
  m_target->FillRectangle(rect, brush(color));
}

void ChromeRenderer::fillRoundRect(const D2D1_RECT_F &rect, float radius, ThemeColor color)
{
  m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush(color));
}

void ChromeRenderer::drawRoundRect(const D2D1_RECT_F &rect, float radius, ThemeColor color,
                                   float strokeWidth)
{
  // The stroke is centered on the outline: inset it by half its width so
  // that it stays inside rect.
  float inset = strokeWidth / 2.0f;
  D2D1_RECT_F inner = D2D1::RectF(rect.left + inset, rect.top + inset,
                                  rect.right - inset, rect.bottom - inset);
  m_target->DrawRoundedRectangle(D2D1::RoundedRect(inner, radius, radius),
                                 brush(color), strokeWidth);
}

IDWriteTextFormat *ChromeRenderer::getTextFormat(float fontSize, DWRITE_FONT_WEIGHT weight)
{
  int sizeKey = static_cast<int>(floorf(fontSize * 100.0f + 0.5f));
  for (size_t i = 0; i < m_formats.size(); i++) {
    if (m_formats[i].sizeKey == sizeKey && m_formats[i].weight == weight) {
      return m_formats[i].format;
    }
  }
  IDWriteFactory *factory = getDWriteFactory();
  if (factory == 0) {
    return 0;
  }
  IDWriteTextFormat *format = 0;
  if (FAILED(factory->CreateTextFormat(m_fontFamily, 0, weight,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       fontSize, L"", &format))) {
    return 0;
  }
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  if (m_ellipsis == 0 && FAILED(factory->CreateEllipsisTrimmingSign(format, &m_ellipsis))) {
    m_ellipsis = 0;
  }
  DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
  format->SetTrimming(&trimming, m_ellipsis);

  CachedFormat cached = { sizeKey, weight, format };
  m_formats.push_back(cached);
  return format;
}

void ChromeRenderer::drawText(const wchar_t *text, const D2D1_RECT_F &rect, ThemeColor color,
                              float fontSize, DWRITE_FONT_WEIGHT weight,
                              DWRITE_TEXT_ALIGNMENT alignment)
{
  IDWriteTextFormat *format = getTextFormat(fontSize, weight);
  if (format == 0 || text == 0) {
    return;
  }
  // The formats are shared by every call: set the alignment each time.
  format->SetTextAlignment(alignment);
  m_target->DrawText(text, static_cast<UINT32>(wcslen(text)), format, rect, brush(color),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void ChromeRenderer::drawGlyph(const wchar_t *glyph, const D2D1_RECT_F &box, ThemeColor color)
{
  // Icon phase: when supportsSvgGlyphs(), look up the SVG document of the
  // glyph and draw it with ID2D1DeviceContext5::DrawSvgDocument, keeping
  // this text placeholder as the fallback of older systems.
  float size = (box.bottom - box.top) * 0.75f;
  drawText(glyph, box, color, size, DWRITE_FONT_WEIGHT_SEMI_BOLD,
           DWRITE_TEXT_ALIGNMENT_CENTER);
}

bool ChromeRenderer::supportsSvgGlyphs()
{
  if (m_svgSupport < 0) {
    if (m_target == 0) {
      return false;
    }
    IUnknown *context = 0;
    m_svgSupport = SUCCEEDED(m_target->QueryInterface(IID_D2D1DeviceContext5,
                                                      reinterpret_cast<void **>(&context))) ? 1 : 0;
    safeRelease(context);
  }
  return m_svgSupport == 1;
}
