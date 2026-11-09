// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// SHA-256 and ECDSA P-256 verification on top of Windows CNG (bcrypt.dll).
// No third-party crypto code.

#pragma once

#include "UpdateTypes.h"

#include <cstddef>
#include <memory>
#include <span>

namespace fsv::update {

// Incremental SHA-256. RAII over a CNG hash object.
class Sha256 {
public:
  Sha256();
  ~Sha256();
  Sha256(const Sha256 &) = delete;
  Sha256 &operator=(const Sha256 &) = delete;

  bool valid() const;                               // false if CNG failed
  void update(const void *data, std::size_t size);
  Sha256Digest finish();                            // single use

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

Sha256Digest sha256(std::span<const std::uint8_t> data);

// Hashes a whole file. Returns nullopt on I/O error. If sizeOut is non-null
// it receives the number of bytes hashed.
std::optional<Sha256Digest> sha256File(const std::wstring &path,
                                       std::uint64_t *sizeOut = nullptr);

std::string toHex(const Sha256Digest &digest);                 // lower-case
std::optional<Sha256Digest> parseSha256Hex(std::string_view hex); // 64 hex chars, any case

// ECDSA P-256 over SHA-256(data). publicKeyBlob is a 72-byte
// BCRYPT_ECCPUBLIC_BLOB (magic ECS1); signature is IEEE P1363 r||s (64 bytes).
// Returns false on any malformed input or verification failure.
bool verifyEcdsaP256(std::span<const std::uint8_t> publicKeyBlob,
                     std::span<const std::uint8_t> data,
                     std::span<const std::uint8_t> signature);

// Update-signing public keys compiled into the binaries (TrustedKeys.cpp).
// Tests pass their own ephemeral keys instead; production code must use this.
struct TrustedKey {
  std::uint32_t id;
  std::span<const std::uint8_t> blob;
};
std::span<const TrustedKey> productionKeys();

} // namespace fsv::update
