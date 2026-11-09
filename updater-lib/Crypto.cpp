// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Crypto.h"

#include "Win32Util.h"

#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace fsv::update {

namespace {

constexpr NTSTATUS kStatusSuccess = 0;
constexpr std::size_t kEccPublicBlobBytes = sizeof(BCRYPT_ECCKEY_BLOB) + 2 * 32; // 72
constexpr std::size_t kP1363SignatureBytes = 64;
constexpr std::size_t kFileReadChunk = 64 * 1024;

} // namespace

// ------------------------------------------------------------------ Sha256

struct Sha256::Impl {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::vector<UCHAR> hashObject;
  bool ok = false;
  bool finished = false;

  Impl() {
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) !=
        kStatusSuccess) {
      algorithm = nullptr;
      return;
    }
    DWORD objectSize = 0;
    DWORD written = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &written,
                          0) != kStatusSuccess ||
        objectSize == 0) {
      return;
    }
    hashObject.resize(objectSize);
    if (BCryptCreateHash(algorithm, &hash, hashObject.data(),
                         static_cast<ULONG>(hashObject.size()), nullptr, 0, 0) !=
        kStatusSuccess) {
      hash = nullptr;
      return;
    }
    ok = true;
  }

  ~Impl() {
    if (hash != nullptr) {
      BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
      BCryptCloseAlgorithmProvider(algorithm, 0);
    }
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
};

Sha256::Sha256() : m_impl(std::make_unique<Impl>()) {}
Sha256::~Sha256() = default;

bool Sha256::valid() const { return m_impl && m_impl->ok && !m_impl->finished; }

void Sha256::update(const void *data, std::size_t size) {
  if (!valid() || size == 0 || data == nullptr) {
    return;
  }
  const auto *bytes = static_cast<const UCHAR *>(data);
  // BCryptHashData takes a ULONG length; feed very large buffers in pieces.
  constexpr std::size_t kMaxPiece = 1u << 30;
  while (size > 0) {
    const std::size_t piece = (std::min)(size, kMaxPiece);
    if (BCryptHashData(m_impl->hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(piece), 0) !=
        kStatusSuccess) {
      m_impl->ok = false;
      return;
    }
    bytes += piece;
    size -= piece;
  }
}

Sha256Digest Sha256::finish() {
  Sha256Digest digest{};
  if (!valid()) {
    return digest;
  }
  if (BCryptFinishHash(m_impl->hash, digest.data(), static_cast<ULONG>(digest.size()), 0) !=
      kStatusSuccess) {
    m_impl->ok = false;
    digest.fill(0);
  }
  m_impl->finished = true;
  return digest;
}

Sha256Digest sha256(std::span<const std::uint8_t> data) {
  Sha256 hasher;
  hasher.update(data.data(), data.size());
  return hasher.finish();
}

std::optional<Sha256Digest> sha256File(const std::wstring &path, std::uint64_t *sizeOut) {
  UniqueHandle file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                  nullptr));
  if (!file.valid()) {
    return std::nullopt;
  }

  Sha256 hasher;
  if (!hasher.valid()) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> buffer(kFileReadChunk);
  std::uint64_t total = 0;
  for (;;) {
    DWORD read = 0;
    if (!::ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                    nullptr)) {
      return std::nullopt;
    }
    if (read == 0) {
      break;
    }
    hasher.update(buffer.data(), read);
    total += read;
  }
  if (!hasher.valid()) {
    return std::nullopt;
  }
  if (sizeOut != nullptr) {
    *sizeOut = total;
  }
  return hasher.finish();
}

std::string toHex(const Sha256Digest &digest) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text;
  text.reserve(digest.size() * 2);
  for (const std::uint8_t byte : digest) {
    text += kDigits[byte >> 4];
    text += kDigits[byte & 0x0F];
  }
  return text;
}

namespace {

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

} // namespace

std::optional<Sha256Digest> parseSha256Hex(std::string_view hex) {
  Sha256Digest digest{};
  if (hex.size() != digest.size() * 2) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < digest.size(); ++i) {
    const int high = hexValue(hex[2 * i]);
    const int low = hexValue(hex[2 * i + 1]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    digest[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return digest;
}

// ------------------------------------------------------------------- ECDSA

bool verifyEcdsaP256(std::span<const std::uint8_t> publicKeyBlob,
                     std::span<const std::uint8_t> data,
                     std::span<const std::uint8_t> signature) {
  if (publicKeyBlob.size() != kEccPublicBlobBytes || signature.size() != kP1363SignatureBytes) {
    return false;
  }
  BCRYPT_ECCKEY_BLOB header{};
  std::memcpy(&header, publicKeyBlob.data(), sizeof(header));
  if (header.dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC || header.cbKey != 32) {
    return false;
  }

  const Sha256Digest digest = sha256(data);

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) !=
      kStatusSuccess) {
    return false;
  }

  bool verified = false;
  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                          const_cast<PUCHAR>(publicKeyBlob.data()),
                          static_cast<ULONG>(publicKeyBlob.size()), 0) == kStatusSuccess) {
    verified = BCryptVerifySignature(key, nullptr, const_cast<PUCHAR>(digest.data()),
                                     static_cast<ULONG>(digest.size()),
                                     const_cast<PUCHAR>(signature.data()),
                                     static_cast<ULONG>(signature.size()), 0) == kStatusSuccess;
    BCryptDestroyKey(key);
  }
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return verified;
}

} // namespace fsv::update
