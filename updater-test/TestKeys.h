// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Test-only helpers: ephemeral ECDSA P-256 keys (CNG) and a unique temporary
// directory. Production code never signs anything.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsv::test {

// An ephemeral ECDSA P-256 key pair that lives only for the test run.
class TestKey {
public:
  TestKey();
  ~TestKey();
  TestKey(const TestKey &) = delete;
  TestKey &operator=(const TestKey &) = delete;

  bool valid() const { return m_key != nullptr; }

  // 72-byte BCRYPT_ECCPUBLIC_BLOB (magic ECS1).
  const std::vector<std::uint8_t> &publicBlob() const { return m_publicBlob; }

  // SHA-256 then ECDSA: a 64-byte IEEE P1363 (r||s) signature, empty on error.
  std::vector<std::uint8_t> sign(std::span<const std::uint8_t> data) const;
  std::vector<std::uint8_t> sign(const std::string &data) const;

private:
  void *m_algorithm = nullptr;
  void *m_key = nullptr;
  std::vector<std::uint8_t> m_publicBlob;
};

// Creates a unique directory under %TEMP% and deletes it recursively on
// destruction.
class TempDir {
public:
  TempDir();
  ~TempDir();
  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;

  const std::wstring &path() const { return m_path; }
  std::wstring child(const std::wstring &name) const;

private:
  std::wstring m_path;
};

std::span<const std::uint8_t> asBytes(const std::string &text);

} // namespace fsv::test
