// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestKeys.h"

#include "updater-lib/Crypto.h"
#include "updater-lib/FsUtil.h"

#include <windows.h>

#include <bcrypt.h>

#include <atomic>
#include <cwchar>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace fsv::test {

std::span<const std::uint8_t> asBytes(const std::string &text) {
  return {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()};
}

// ----------------------------------------------------------------- TestKey

TestKey::TestKey() {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) {
    return;
  }
  m_algorithm = algorithm;

  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCryptGenerateKeyPair(algorithm, &key, 256, 0) < 0) {
    return;
  }
  if (BCryptFinalizeKeyPair(key, 0) < 0) {
    BCryptDestroyKey(key);
    return;
  }

  std::vector<std::uint8_t> blob(72);
  ULONG written = 0;
  if (BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(),
                      static_cast<ULONG>(blob.size()), &written, 0) < 0 ||
      written != blob.size()) {
    BCryptDestroyKey(key);
    return;
  }
  m_key = key;
  m_publicBlob = std::move(blob);
}

TestKey::~TestKey() {
  if (m_key != nullptr) {
    BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(m_key));
  }
  if (m_algorithm != nullptr) {
    BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_algorithm), 0);
  }
}

std::vector<std::uint8_t> TestKey::sign(std::span<const std::uint8_t> data) const {
  if (m_key == nullptr) {
    return {};
  }
  const fsv::update::Sha256Digest digest = fsv::update::sha256(data);
  std::vector<std::uint8_t> signature(64);
  ULONG written = 0;
  if (BCryptSignHash(static_cast<BCRYPT_KEY_HANDLE>(m_key), nullptr,
                     const_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()),
                     signature.data(), static_cast<ULONG>(signature.size()), &written, 0) < 0 ||
      written != signature.size()) {
    return {};
  }
  return signature;
}

std::vector<std::uint8_t> TestKey::sign(const std::string &data) const {
  return sign(asBytes(data));
}

// ----------------------------------------------------------------- TempDir

TempDir::TempDir() {
  static std::atomic<unsigned> counter{0};
  wchar_t temp[MAX_PATH + 2] = {};
  const DWORD length = ::GetTempPathW(MAX_PATH + 1, temp);
  if (length == 0 || length > MAX_PATH) {
    return;
  }
  std::uint8_t random[4] = {};
  BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  wchar_t name[96] = {};
  std::swprintf(name, sizeof(name) / sizeof(name[0]), L"fsv-updater-test-%lu-%u-%02x%02x%02x%02x",
                static_cast<unsigned long>(::GetCurrentProcessId()), ++counter, random[0],
                random[1], random[2], random[3]);
  const std::wstring path = fsv::update::joinPath(fsv::update::fullPath(temp), name);
  if (fsv::update::ensureDirectory(path)) {
    m_path = path;
  }
}

TempDir::~TempDir() {
  if (!m_path.empty()) {
    fsv::update::removeTree(m_path, fsv::update::parentDirectory(m_path));
  }
}

std::wstring TempDir::child(const std::wstring &name) const {
  return fsv::update::joinPath(m_path, name);
}

} // namespace fsv::test
