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

// ID2D1DeviceContext5 and the SVG document API are declared for Windows 10
// (d2d1_3.h); util/winhdr.h would fix WINVER 0x0501 and hide them, so this
// file sets the version before any header (as ReconnectDialog.cpp does). The
// interface is used only after QueryInterface() succeeds at run time.
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0A00
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00
#ifdef NTDDI_VERSION
#undef NTDDI_VERSION
#endif
#define NTDDI_VERSION 0x0A000003

#include <windows.h>
#include <objidl.h>
#include <d2d1_3.h>
#include <stdio.h>
#include <string>

#include "SvgIcons.h"

namespace {

// Size of the Lucide viewBox.
const float kIconViewBox = 24.0f;

template <class T> void safeRelease(T *&object)
{
  if (object != 0) {
    object->Release();
    object = 0;
  }
}

// Text of an RCDATA resource of the executable, or an empty string.
bool loadResource(int resourceId, std::string *text)
{
  HMODULE module = GetModuleHandle(0);
  HRSRC info = FindResource(module, MAKEINTRESOURCE(resourceId), RT_RCDATA);
  if (info == 0) {
    return false;
  }
  HGLOBAL handle = LoadResource(module, info);
  DWORD size = SizeofResource(module, info);
  const void *data = handle != 0 ? LockResource(handle) : 0;
  if (data == 0 || size == 0) {
    return false;
  }
  text->assign(static_cast<const char *>(data), size);
  return true;
}

// Replaces every stroke="currentColor" (and fill="currentColor") with color.
void applyColor(std::string *svg, ThemeColor color)
{
  char hex[8];
  sprintf_s(hex, "#%06X", color & 0xFFFFFF);
  const std::string from = "\"currentColor\"";
  const std::string to = std::string("\"") + hex + "\"";
  for (size_t pos = svg->find(from); pos != std::string::npos;
       pos = svg->find(from, pos + to.size())) {
    svg->replace(pos, from.size(), to);
  }
}

// Read-only stream over a copy of text.
IStream *createStream(const std::string &text)
{
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size());
  if (memory == 0) {
    return 0;
  }
  void *bytes = GlobalLock(memory);
  if (bytes == 0) {
    GlobalFree(memory);
    return 0;
  }
  memcpy(bytes, text.data(), text.size());
  GlobalUnlock(memory);
  IStream *stream = 0;
  // The stream frees the memory when it is released.
  if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
    GlobalFree(memory);
    return 0;
  }
  return stream;
}

}

SvgIconCache::SvgIconCache()
: m_context(0)
{
}

SvgIconCache::~SvgIconCache()
{
  release();
}

bool SvgIconCache::attach(ID2D1RenderTarget *target)
{
  release();
  if (target == 0) {
    return false;
  }
  if (FAILED(target->QueryInterface(__uuidof(ID2D1DeviceContext5),
                                    reinterpret_cast<void **>(&m_context)))) {
    m_context = 0;
    return false;
  }
  return true;
}

void SvgIconCache::release()
{
  for (size_t i = 0; i < m_entries.size(); i++) {
    safeRelease(m_entries[i].document);
  }
  m_entries.clear();
  safeRelease(m_context);
}

ID2D1SvgDocument *SvgIconCache::document(int resourceId, ThemeColor color)
{
  for (size_t i = 0; i < m_entries.size(); i++) {
    if (m_entries[i].resourceId == resourceId && m_entries[i].color == color) {
      return m_entries[i].document;
    }
  }
  Entry entry = { resourceId, color, 0 };
  std::string svg;
  if (loadResource(resourceId, &svg)) {
    applyColor(&svg, color);
    IStream *stream = createStream(svg);
    if (stream != 0) {
      if (FAILED(m_context->CreateSvgDocument(stream, D2D1::SizeF(kIconViewBox, kIconViewBox),
                                               &entry.document))) {
        entry.document = 0;
      }
      stream->Release();
    }
  }
  m_entries.push_back(entry);
  return entry.document;
}

bool SvgIconCache::draw(int resourceId, const D2D1_RECT_F &box, ThemeColor color)
{
  if (m_context == 0) {
    return false;
  }
  ID2D1SvgDocument *svg = document(resourceId, color);
  if (svg == 0) {
    return false;
  }
  float width = box.right - box.left;
  float height = box.bottom - box.top;
  float side = width < height ? width : height;
  if (side <= 0.0f) {
    return false;
  }
  float scale = side / kIconViewBox;
  D2D1_MATRIX_3X2_F previous;
  m_context->GetTransform(&previous);
  D2D1_MATRIX_3X2_F transform =
    D2D1::Matrix3x2F::Scale(scale, scale) *
    D2D1::Matrix3x2F::Translation(box.left + (width - side) / 2.0f,
                                  box.top + (height - side) / 2.0f) *
    D2D1::Matrix3x2F(previous._11, previous._12, previous._21, previous._22,
                     previous._31, previous._32);
  m_context->SetTransform(transform);
  m_context->DrawSvgDocument(svg);
  m_context->SetTransform(previous);
  return true;
}
