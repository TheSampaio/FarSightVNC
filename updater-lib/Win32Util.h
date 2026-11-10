// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Internal helpers for the updater-lib sources. This header includes
// <windows.h>, so it is NOT part of the public interface and must not be
// included by the public headers.

#pragma once

#include <windows.h>

#include <utility>

namespace fsv::update {

// Owns a Win32 kernel HANDLE (file, process, event...). Both nullptr and
// INVALID_HANDLE_VALUE mean "empty".
class UniqueHandle {
public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) : m_handle(handle) {}
  ~UniqueHandle() { reset(); }

  UniqueHandle(const UniqueHandle &) = delete;
  UniqueHandle &operator=(const UniqueHandle &) = delete;
  UniqueHandle(UniqueHandle &&other) noexcept
      : m_handle(std::exchange(other.m_handle, nullptr)) {}
  UniqueHandle &operator=(UniqueHandle &&other) noexcept {
    if (this != &other) {
      reset(std::exchange(other.m_handle, nullptr));
    }
    return *this;
  }

  bool valid() const { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }
  HANDLE get() const { return m_handle; }

  void reset(HANDLE handle = nullptr) {
    if (valid()) {
      ::CloseHandle(m_handle);
    }
    m_handle = handle;
  }

private:
  HANDLE m_handle = nullptr;
};

// Runs `fn` and returns its result; any exception (for example
// std::bad_alloc) is swallowed and `fallback` returned instead. Public
// functions of updater-lib must not let exceptions escape.
template <class Fn, class R>
R guarded(Fn &&fn, R fallback) noexcept {
  try {
    return fn();
  } catch (...) {
    return fallback;
  }
}

} // namespace fsv::update
