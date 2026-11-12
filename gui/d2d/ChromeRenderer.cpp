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
// Modified for FarSightVNC on 2026-11-12: drawSvgIcon() draws the embedded Lucide SVG icons (SvgIconCache).
// Modified for FarSightVNC on 2026-11-12: drawIconResource() draws an ICON resource of the executable.
//

#include "ChromeRenderer.h"
#include "SvgIcons.h"

#include <math.h>

namespace {

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

// Family of the system icon font, or 0 when none is installed. Checked once.
const wchar_t *iconFontFamily(IDWriteFactory *factory)
{
  static bool checked = false;
  static const wchar_t *family = 0;
  if (checked || factory == 0) {
    return family;
  }
  checked = true;
  IDWriteFontCollection *collection = 0;
  if (FAILED(factory->GetSystemFontCollection(&collection, FALSE)) || collection == 0) {
    return family;
  }
  static const wchar_t *const candidates[] = { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" };
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && family == 0; i++) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(collection->FindFamilyName(candidates[i], &index, &exists)) && exists) {
      family = candidates[i];
    }
  }
  collection->Release();
  return family;
}

D2D1_COLOR_F toColor(ThemeColor color)
{
  return D2D1::ColorF(color);
}

// Reads bitmap as width x height top-down 32 bpp BGRA into pixels (GDI
// converts whatever format the bitmap has). The alpha is straight, as the
// icon stores it.
bool readBitmapPixels(HBITMAP bitmap, int width, int height, std::vector<BYTE> *pixels)
{
  HDC dc = CreateCompatibleDC(0);
  if (dc == 0) {
    return false;
  }
  BITMAPINFO info;
  ZeroMemory(&info, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;  // Top-down.
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  pixels->resize(static_cast<size_t>(width) * height * 4);
  bool ok = GetDIBits(dc, bitmap, 0, height, &(*pixels)[0], &info, DIB_RGB_COLORS) == height;
  DeleteDC(dc);
  return ok;
}

bool hasAlphaChannel(const std::vector<BYTE> &pixels)
{
  for (size_t i = 3; i < pixels.size(); i += 4) {
    if (pixels[i] != 0) {
      return true;
    }
  }
  return false;
}

// Rebuilds the alpha of an icon without an alpha channel from its AND mask:
// a mask pixel that is set (white) is transparent, the others are opaque.
void applyIconMask(HBITMAP mask, int width, int height, std::vector<BYTE> *pixels)
{
  std::vector<BYTE> maskPixels;
  if (mask == 0 || !readBitmapPixels(mask, width, height, &maskPixels) ||
      maskPixels.size() != pixels->size()) {
    // Without a usable mask the whole icon stays opaque.
    for (size_t i = 3; i < pixels->size(); i += 4) {
      (*pixels)[i] = 255;
    }
    return;
  }
  for (size_t i = 0; i + 3 < pixels->size(); i += 4) {
    bool transparent = maskPixels[i] != 0 || maskPixels[i + 1] != 0 || maskPixels[i + 2] != 0;
    (*pixels)[i + 3] = transparent ? 0 : 255;
  }
}

// Direct2D wants premultiplied alpha; the icon gives it straight.
void premultiplyAlpha(std::vector<BYTE> *pixels)
{
  for (size_t i = 0; i + 3 < pixels->size(); i += 4) {
    unsigned int alpha = (*pixels)[i + 3];
    for (size_t channel = 0; channel < 3; channel++) {
      (*pixels)[i + channel] = static_cast<BYTE>((*pixels)[i + channel] * alpha / 255);
    }
  }
}

}

ChromeRenderer::ChromeRenderer()
: m_ellipsis(0),
  m_hwnd(0),
  m_dpi(96),
  // Same family in the light and the dark palette: no theme to follow.
  m_fontFamily(kLightTheme.fontFamily),
  m_target(0),
  m_hwndTarget(0),
  m_dcTarget(0),
  m_layered(false),
  m_layeredDc(0),
  m_layeredBitmap(0),
  m_layeredOldBitmap(0),
  m_brush(0),
  m_svgSupport(-1),
  m_svgIcons(new SvgIconCache)
{
  m_layeredSize.cx = m_layeredSize.cy = 0;
  m_layeredBitmapSize.cx = m_layeredBitmapSize.cy = 0;
}

ChromeRenderer::~ChromeRenderer()
{
  discardTarget();
  releaseIconBitmaps();
  delete m_svgIcons;
  releaseLayeredBitmap();
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
  m_layered = false;
  m_fontFamily = fontFamily;
  setDpi(dpi);
}

void ChromeRenderer::attachLayered(HWND hwnd, UINT dpi, const wchar_t *fontFamily)
{
  if (hwnd != m_hwnd || !m_layered) {
    discardTarget();
  }
  m_hwnd = hwnd;
  m_layered = true;
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
  if (m_layered) {
    // The bitmap is made again at the next beginDraw().
    m_layeredSize.cx = static_cast<LONG>(width);
    m_layeredSize.cy = static_cast<LONG>(height);
    return;
  }
  if (m_hwndTarget != 0 && FAILED(m_hwndTarget->Resize(D2D1::SizeU(width, height)))) {
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
  if (m_layered) {
    // Premultiplied alpha, as UpdateLayeredWindow() expects it.
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    if (FAILED(factory->CreateDCRenderTarget(&properties, &m_dcTarget))) {
      m_dcTarget = 0;
      return false;
    }
    m_target = m_dcTarget;
  } else {
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
    if (FAILED(factory->CreateHwndRenderTarget(properties, hwndProperties, &m_hwndTarget))) {
      m_hwndTarget = 0;
      return false;
    }
    m_target = m_hwndTarget;
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
  // The SVG documents and the icon bitmaps belong to the device of the target.
  m_svgIcons->release();
  releaseIconBitmaps();
  safeRelease(m_brush);
  m_target = 0;
  safeRelease(m_hwndTarget);
  safeRelease(m_dcTarget);
  m_svgSupport = -1;
}

bool ChromeRenderer::prepareLayeredBitmap()
{
  if (m_layeredSize.cx <= 0 || m_layeredSize.cy <= 0) {
    return false;
  }
  if (m_layeredBitmap != 0 && m_layeredBitmapSize.cx == m_layeredSize.cx &&
      m_layeredBitmapSize.cy == m_layeredSize.cy) {
    return true;
  }
  releaseLayeredBitmap();
  BITMAPINFO info;
  ZeroMemory(&info, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = m_layeredSize.cx;
  info.bmiHeader.biHeight = -m_layeredSize.cy;  // Top-down.
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *bits = 0;
  m_layeredDc = CreateCompatibleDC(0);
  if (m_layeredDc == 0) {
    return false;
  }
  m_layeredBitmap = CreateDIBSection(m_layeredDc, &info, DIB_RGB_COLORS, &bits, 0, 0);
  if (m_layeredBitmap == 0) {
    releaseLayeredBitmap();
    return false;
  }
  m_layeredOldBitmap = SelectObject(m_layeredDc, m_layeredBitmap);
  m_layeredBitmapSize = m_layeredSize;
  return true;
}

void ChromeRenderer::releaseLayeredBitmap()
{
  if (m_layeredDc != 0) {
    if (m_layeredOldBitmap != 0) {
      SelectObject(m_layeredDc, m_layeredOldBitmap);
    }
    DeleteDC(m_layeredDc);
  }
  if (m_layeredBitmap != 0) {
    DeleteObject(m_layeredBitmap);
  }
  m_layeredDc = 0;
  m_layeredBitmap = 0;
  m_layeredOldBitmap = 0;
  m_layeredBitmapSize.cx = m_layeredBitmapSize.cy = 0;
}

bool ChromeRenderer::beginDraw()
{
  if (!createTarget()) {
    return false;
  }
  if (m_layered) {
    RECT area = { 0, 0, m_layeredSize.cx, m_layeredSize.cy };
    if (!prepareLayeredBitmap() || FAILED(m_dcTarget->BindDC(m_layeredDc, &area))) {
      return false;
    }
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
    if (!m_layered) {
      InvalidateRect(m_hwnd, 0, FALSE);
    }
    return;
  }
  if (m_layered && SUCCEEDED(hr)) {
    POINT source = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, 0, 0, &m_layeredSize, m_layeredDc, &source, 0, &blend,
                        ULW_ALPHA);
  }
}

ID2D1SolidColorBrush *ChromeRenderer::brush(ThemeColor color, float opacity)
{
  m_brush->SetColor(toColor(color));
  m_brush->SetOpacity(opacity);
  return m_brush;
}

void ChromeRenderer::clear(ThemeColor color)
{
  m_target->Clear(toColor(color));
}

void ChromeRenderer::clearTransparent()
{
  m_target->Clear(D2D1::ColorF(0, 0.0f));
}

void ChromeRenderer::fillRect(const D2D1_RECT_F &rect, ThemeColor color)
{
  m_target->FillRectangle(rect, brush(color));
}

void ChromeRenderer::fillRoundRect(const D2D1_RECT_F &rect, float radius, ThemeColor color,
                                   float opacity)
{
  m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush(color, opacity));
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
  return getFormat(m_fontFamily, fontSize, weight);
}

IDWriteTextFormat *ChromeRenderer::getFormat(const wchar_t *family, float fontSize,
                                             DWRITE_FONT_WEIGHT weight)
{
  int sizeKey = static_cast<int>(floorf(fontSize * 100.0f + 0.5f));
  for (size_t i = 0; i < m_formats.size(); i++) {
    if (m_formats[i].sizeKey == sizeKey && m_formats[i].weight == weight &&
        wcscmp(m_formats[i].family, family) == 0) {
      return m_formats[i].format;
    }
  }
  IDWriteFactory *factory = getDWriteFactory();
  if (factory == 0) {
    return 0;
  }
  IDWriteTextFormat *format = 0;
  if (FAILED(factory->CreateTextFormat(family, 0, weight,
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

  CachedFormat cached = { family, sizeKey, weight, format };
  m_formats.push_back(cached);
  return format;
}

float ChromeRenderer::measureText(const wchar_t *text, float fontSize, DWRITE_FONT_WEIGHT weight)
{
  IDWriteTextFormat *format = getTextFormat(fontSize, weight);
  IDWriteFactory *factory = getDWriteFactory();
  if (format == 0 || factory == 0 || text == 0) {
    return 0.0f;
  }
  IDWriteTextLayout *layout = 0;
  if (FAILED(factory->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)), format,
                                       100000.0f, 1000.0f, &layout))) {
    return 0.0f;
  }
  DWRITE_TEXT_METRICS metrics;
  float width = SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace
                                                        : 0.0f;
  layout->Release();
  return width;
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

IDWriteTextLayout *ChromeRenderer::createParagraphLayout(const wchar_t *text, float width,
                                                         float height, float fontSize,
                                                         DWRITE_FONT_WEIGHT weight)
{
  IDWriteTextFormat *format = getTextFormat(fontSize, weight);
  IDWriteFactory *factory = getDWriteFactory();
  if (format == 0 || factory == 0 || text == 0 || width <= 0.0f) {
    return 0;
  }
  IDWriteTextLayout *layout = 0;
  if (FAILED(factory->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)), format,
                                       width, height > 0.0f ? height : 100000.0f, &layout))) {
    return 0;
  }
  // The shared format is a single trimmed line: the layout overrides it.
  layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
  layout->SetTrimming(&trimming, 0);
  return layout;
}

void ChromeRenderer::drawParagraph(const wchar_t *text, const D2D1_RECT_F &rect, ThemeColor color,
                                   float fontSize, DWRITE_FONT_WEIGHT weight)
{
  IDWriteTextLayout *layout = createParagraphLayout(text, rect.right - rect.left,
                                                    rect.bottom - rect.top, fontSize, weight);
  if (layout == 0) {
    return;
  }
  m_target->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout, brush(color),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
  layout->Release();
}

float ChromeRenderer::measureParagraph(const wchar_t *text, float width, float fontSize,
                                       DWRITE_FONT_WEIGHT weight)
{
  IDWriteTextLayout *layout = createParagraphLayout(text, width, 0.0f, fontSize, weight);
  if (layout == 0) {
    return 0.0f;
  }
  DWRITE_TEXT_METRICS metrics;
  float height = SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.height : 0.0f;
  layout->Release();
  return height;
}

void ChromeRenderer::drawGlyph(const wchar_t *glyph, const D2D1_RECT_F &box, ThemeColor color)
{
  float size = (box.bottom - box.top) * 0.75f;
  drawText(glyph, box, color, size, DWRITE_FONT_WEIGHT_SEMI_BOLD,
           DWRITE_TEXT_ALIGNMENT_CENTER);
}

bool ChromeRenderer::drawIconGlyph(wchar_t codepoint, const D2D1_RECT_F &box, ThemeColor color,
                                   float size)
{
  const wchar_t *family = iconFontFamily(getDWriteFactory());
  if (family == 0) {
    return false;
  }
  IDWriteTextFormat *format = getFormat(family, size, DWRITE_FONT_WEIGHT_NORMAL);
  if (format == 0) {
    return false;
  }
  format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  wchar_t text[2] = { codepoint, 0 };
  m_target->DrawText(text, 1, format, box, brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  return true;
}

bool ChromeRenderer::supportsSvgGlyphs()
{
  if (m_svgSupport < 0) {
    if (m_target == 0) {
      return false;
    }
    m_svgSupport = m_svgIcons->attach(m_target) ? 1 : 0;
  }
  return m_svgSupport == 1;
}

bool ChromeRenderer::drawSvgIcon(int resourceId, const D2D1_RECT_F &box, ThemeColor color)
{
  return supportsSvgGlyphs() && m_svgIcons->draw(resourceId, box, color);
}

void ChromeRenderer::releaseIconBitmaps()
{
  for (size_t i = 0; i < m_icons.size(); i++) {
    safeRelease(m_icons[i].bitmap);
  }
  m_icons.clear();
}

ID2D1Bitmap *ChromeRenderer::createIconBitmap(int resourceId, int pixelSize)
{
  // No WIC here: the viewer never calls CoInitialize(). LoadImage() picks the
  // entry of the .ico closest to pixelSize and scales it, and GDI hands the
  // pixels over as BGRA.
  HICON icon = static_cast<HICON>(LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(resourceId),
                                            IMAGE_ICON, pixelSize, pixelSize, LR_DEFAULTCOLOR));
  if (icon == 0) {
    return 0;
  }
  ICONINFO info;
  ZeroMemory(&info, sizeof(info));
  if (!GetIconInfo(icon, &info)) {
    DestroyIcon(icon);
    return 0;
  }
  ID2D1Bitmap *bitmap = 0;
  BITMAP header;
  std::vector<BYTE> pixels;
  if (info.hbmColor != 0 && GetObject(info.hbmColor, sizeof(header), &header) != 0 &&
      header.bmWidth > 0 && header.bmHeight > 0 &&
      readBitmapPixels(info.hbmColor, header.bmWidth, header.bmHeight, &pixels)) {
    // An icon without an alpha channel comes back fully transparent: its
    // shape is in the mask.
    if (!hasAlphaChannel(pixels)) {
      applyIconMask(info.hbmMask, header.bmWidth, header.bmHeight, &pixels);
    }
    premultiplyAlpha(&pixels);
    D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(m_target->CreateBitmap(
          D2D1::SizeU(static_cast<UINT32>(header.bmWidth), static_cast<UINT32>(header.bmHeight)),
          &pixels[0], static_cast<UINT32>(header.bmWidth) * 4, properties, &bitmap))) {
      bitmap = 0;
    }
  }
  if (info.hbmColor != 0) {
    DeleteObject(info.hbmColor);
  }
  if (info.hbmMask != 0) {
    DeleteObject(info.hbmMask);
  }
  DestroyIcon(icon);
  return bitmap;
}

ID2D1Bitmap *ChromeRenderer::getIconBitmap(int resourceId, int pixelSize)
{
  for (size_t i = 0; i < m_icons.size(); i++) {
    if (m_icons[i].resourceId == resourceId && m_icons[i].pixelSize == pixelSize) {
      return m_icons[i].bitmap;
    }
  }
  // A failed load is remembered as a null entry, so it is not retried at
  // every paint.
  CachedIcon cached = { resourceId, pixelSize, createIconBitmap(resourceId, pixelSize) };
  m_icons.push_back(cached);
  return cached.bitmap;
}

bool ChromeRenderer::drawIconResource(int resourceId, const D2D1_RECT_F &box)
{
  if (m_target == 0) {
    return false;
  }
  float width = box.right - box.left;
  float height = box.bottom - box.top;
  float side = width < height ? width : height;
  if (side <= 0.0f) {
    return false;
  }
  int pixelSize = dipsToPixels(side);
  if (pixelSize < 1) {
    pixelSize = 1;
  }
  ID2D1Bitmap *bitmap = getIconBitmap(resourceId, pixelSize);
  if (bitmap == 0) {
    return false;
  }
  float centerX = (box.left + box.right) / 2.0f;
  float centerY = (box.top + box.bottom) / 2.0f;
  float half = side / 2.0f;
  D2D1_RECT_F dest = D2D1::RectF(centerX - half, centerY - half,
                                 centerX + half, centerY + half);
  m_target->DrawBitmap(bitmap, dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  return true;
}
