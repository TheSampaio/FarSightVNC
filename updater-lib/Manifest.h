// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Signed update manifest. The manifest is the root of trust for a release:
// its signature is checked against the compiled-in public keys BEFORE any
// byte of it is parsed, and it pins the package and every installed file by
// size and SHA-256. Grammar (UTF-8, LF or CRLF, no other lines, single spaces):
//
//   FarSightVNC-Update-Manifest 1
//   version 1.0.0a
//   package FarSightVNC-v1.0.0a.zip <size> <sha256-hex>
//   file fsvncviewer.exe <size> <sha256-hex>
//   file fsvncupdater.exe <size> <sha256-hex>
//   ...

#pragma once

#include "Crypto.h"

namespace fsv::update {

inline constexpr char kManifestAssetName[] = "farsightvnc-update.manifest";
inline constexpr char kSignatureAssetName[] = "farsightvnc-update.manifest.sig";
inline constexpr std::size_t kSignatureBytes = 64;
inline constexpr std::size_t kMaxManifestBytes = 64 * 1024;
inline constexpr std::uint64_t kMaxPackageBytes = 128ull * 1024 * 1024;
inline constexpr std::uint64_t kMaxFileBytes = 64ull * 1024 * 1024;
inline constexpr std::size_t kMaxManifestFiles = 64;

struct ManifestFile {
  std::string path; // relative, '/'-separated, passes isSafeRelativePath
  std::uint64_t size = 0;
  Sha256Digest sha256{};
};

struct Manifest {
  Version version;
  std::string packageName; // single safe segment ending in ".zip"
  std::uint64_t packageSize = 0;
  Sha256Digest packageSha256{};
  std::vector<ManifestFile> files; // non-empty, no case-insensitive duplicates
};

// 1-4 segments of [A-Za-z0-9._-], max 200 chars total; rejects "", ".", "..",
// leading/trailing dot in a segment, Windows device names (CON, PRN, AUX, NUL,
// COM1-9, LPT1-9, with or without extension), '\\', ':', absolute paths.
bool isSafeRelativePath(std::string_view path);

// Strict parse of an already-authenticated manifest (exposed for tests).
std::optional<Manifest> parseManifest(std::string_view text);

enum class ManifestError { None, TooLarge, BadSignature, Malformed };

// The only entry point production code may use: verifies `signature` over the
// exact manifest bytes with each key in `keys`; only on success parses it.
std::optional<Manifest> verifyAndParseManifest(std::span<const std::uint8_t> manifestBytes,
                                               std::span<const std::uint8_t> signature,
                                               std::span<const TrustedKey> keys,
                                               ManifestError *error = nullptr);

} // namespace fsv::update
