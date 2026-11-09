// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "ZipReader.h"

#include "FsUtil.h"
#include "Win32Util.h"

#include "vendor/zlib/zlib.h"

#include <algorithm>
#include <cstring>

namespace fsv::update {

namespace {

constexpr std::uint32_t kEocdSignature = 0x06054b50;
constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::size_t kEocdSize = 22;
constexpr std::size_t kCentralHeaderSize = 46;
constexpr std::size_t kLocalHeaderSize = 30;
constexpr std::size_t kMaxEocdSearch = 65535 + kEocdSize;
constexpr std::uint64_t kMaxCentralDirectoryBytes = 16ull * 1024 * 1024;
constexpr std::size_t kChunkBytes = 64 * 1024;

constexpr std::uint16_t kFlagEncrypted = 0x0001;
constexpr std::uint16_t kFlagStrongEncryption = 0x0040;
constexpr std::uint16_t kFlagMaskedHeaders = 0x2000;
constexpr std::uint16_t kMethodStored = 0;
constexpr std::uint16_t kMethodDeflate = 8;

std::uint16_t readU16(const std::uint8_t *p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t readU32(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Releases a zlib inflate stream on every path.
class InflateStream {
public:
  InflateStream() { std::memset(&m_stream, 0, sizeof(m_stream)); }
  ~InflateStream() {
    if (m_initialized) {
      inflateEnd(&m_stream);
    }
  }
  InflateStream(const InflateStream &) = delete;
  InflateStream &operator=(const InflateStream &) = delete;

  bool init() {
    m_initialized = inflateInit2(&m_stream, -MAX_WBITS) == Z_OK;
    return m_initialized;
  }
  z_stream &get() { return m_stream; }

private:
  z_stream m_stream;
  bool m_initialized = false;
};

struct CentralEntry {
  std::size_t manifestIndex = 0;
  std::uint16_t flags = 0;
  std::uint16_t method = 0;
  std::uint32_t crc = 0;
  std::uint32_t compressedSize = 0;
  std::uint32_t uncompressedSize = 0;
  std::uint32_t localHeaderOffset = 0;
};

class ZipExtractor {
public:
  ZipExtractor(const std::wstring &zipPath, const Manifest &manifest, const std::wstring &destRoot)
      : m_zipPath(zipPath), m_manifest(manifest), m_destRoot(destRoot) {}

  bool run() {
    if (m_destRoot.empty() || m_manifest.files.empty()) {
      return fail("invalid arguments");
    }
    m_file.reset(::CreateFileW(m_zipPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!m_file.valid()) {
      return fail("cannot open package");
    }
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(m_file.get(), &size) || size.QuadPart < 0) {
      return fail("cannot get package size");
    }
    m_fileSize = static_cast<std::uint64_t>(size.QuadPart);
    if (m_fileSize < kEocdSize) {
      return fail("package is not a zip archive");
    }
    if (!readCentralDirectory()) {
      return false;
    }
    if (!matchEntries()) {
      return false;
    }
    for (const CentralEntry &entry : m_entries) {
      if (!extract(entry)) {
        return false;
      }
    }
    return true;
  }

  const std::string &error() const { return m_error; }

private:
  bool fail(const char *message) {
    if (m_error.empty()) {
      m_error = message;
    }
    return false;
  }

  bool readAt(std::uint64_t offset, void *buffer, std::size_t length) {
    if (length == 0) {
      return true;
    }
    if (offset > m_fileSize || length > m_fileSize - offset) {
      return false;
    }
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!::SetFilePointerEx(m_file.get(), position, nullptr, FILE_BEGIN)) {
      return false;
    }
    auto *out = static_cast<std::uint8_t *>(buffer);
    while (length > 0) {
      DWORD got = 0;
      const DWORD want = static_cast<DWORD>(std::min<std::size_t>(length, 1u << 20));
      if (!::ReadFile(m_file.get(), out, want, &got, nullptr) || got == 0) {
        return false;
      }
      out += got;
      length -= got;
    }
    return true;
  }

  bool readCentralDirectory() {
    const std::size_t tailSize =
        static_cast<std::size_t>(std::min<std::uint64_t>(m_fileSize, kMaxEocdSearch));
    const std::uint64_t tailOffset = m_fileSize - tailSize;
    std::vector<std::uint8_t> tail(tailSize);
    if (!readAt(tailOffset, tail.data(), tail.size())) {
      return fail("cannot read package end");
    }

    // Scan backwards for the EOCD whose comment ends exactly at the file end.
    std::size_t eocd = tailSize;
    for (std::size_t pos = tailSize - kEocdSize + 1; pos-- > 0;) {
      if (readU32(&tail[pos]) != kEocdSignature) {
        continue;
      }
      const std::size_t commentLength = readU16(&tail[pos + 20]);
      if (pos + kEocdSize + commentLength == tailSize) {
        eocd = pos;
        break;
      }
    }
    if (eocd == tailSize) {
      return fail("end of central directory not found");
    }
    const std::uint64_t eocdOffset = tailOffset + eocd;
    const std::uint8_t *record = &tail[eocd];

    if (eocdOffset >= 20) {
      std::uint8_t locator[4];
      if (readAt(eocdOffset - 20, locator, sizeof(locator)) &&
          readU32(locator) == kZip64LocatorSignature) {
        return fail("ZIP64 archives are not supported");
      }
    }
    const std::uint16_t diskNumber = readU16(record + 4);
    const std::uint16_t cdDisk = readU16(record + 6);
    const std::uint16_t entriesOnDisk = readU16(record + 8);
    const std::uint16_t totalEntries = readU16(record + 10);
    const std::uint32_t cdSize = readU32(record + 12);
    const std::uint32_t cdOffset = readU32(record + 16);
    if (diskNumber != 0 || cdDisk != 0 || entriesOnDisk != totalEntries) {
      return fail("multi-disk archives are not supported");
    }
    if (totalEntries == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
      return fail("ZIP64 archives are not supported");
    }
    if (cdSize > kMaxCentralDirectoryBytes ||
        static_cast<std::uint64_t>(cdOffset) + cdSize > eocdOffset) {
      return fail("central directory is out of range");
    }
    m_centralOffset = cdOffset;
    m_entryCount = totalEntries;
    m_central.resize(cdSize);
    if (!readAt(cdOffset, m_central.data(), m_central.size())) {
      return fail("cannot read central directory");
    }
    return true;
  }

  bool matchEntries() {
    std::vector<std::string> wanted;
    wanted.reserve(m_manifest.files.size());
    for (const ManifestFile &file : m_manifest.files) {
      std::string key = file.path;
      std::transform(key.begin(), key.end(), key.begin(), lowerAscii);
      wanted.push_back(std::move(key));
    }
    std::vector<bool> found(wanted.size(), false);

    std::size_t pos = 0;
    for (std::size_t i = 0; i < m_entryCount; ++i) {
      if (m_central.size() - pos < kCentralHeaderSize ||
          readU32(&m_central[pos]) != kCentralSignature) {
        return fail("corrupt central directory");
      }
      const std::uint8_t *h = &m_central[pos];
      const std::size_t nameLength = readU16(h + 28);
      const std::size_t extraLength = readU16(h + 30);
      const std::size_t commentLength = readU16(h + 32);
      const std::size_t total = kCentralHeaderSize + nameLength + extraLength + commentLength;
      if (total > m_central.size() - pos) {
        return fail("corrupt central directory");
      }

      std::string name(reinterpret_cast<const char *>(h + kCentralHeaderSize), nameLength);
      pos += total;
      for (char &c : name) {
        c = (c == '\\') ? '/' : lowerAscii(c);
      }
      const auto it = std::find(wanted.begin(), wanted.end(), name);
      if (it == wanted.end()) {
        continue; // directory or unlisted entry
      }
      const std::size_t index = static_cast<std::size_t>(it - wanted.begin());
      if (found[index]) {
        return fail("duplicate archive entry for a listed file");
      }
      found[index] = true;

      CentralEntry entry;
      entry.manifestIndex = index;
      entry.flags = readU16(h + 8);
      entry.method = readU16(h + 10);
      entry.crc = readU32(h + 16);
      entry.compressedSize = readU32(h + 20);
      entry.uncompressedSize = readU32(h + 24);
      entry.localHeaderOffset = readU32(h + 42);
      if (readU16(h + 34) != 0) {
        return fail("multi-disk archives are not supported");
      }
      if (entry.compressedSize == 0xFFFFFFFFu || entry.uncompressedSize == 0xFFFFFFFFu ||
          entry.localHeaderOffset == 0xFFFFFFFFu) {
        return fail("ZIP64 entries are not supported");
      }
      if ((entry.flags & (kFlagEncrypted | kFlagStrongEncryption | kFlagMaskedHeaders)) != 0) {
        return fail("encrypted entries are not supported");
      }
      if (entry.method != kMethodStored && entry.method != kMethodDeflate) {
        return fail("unsupported compression method");
      }
      if (entry.uncompressedSize != m_manifest.files[index].size) {
        return fail("entry size does not match the manifest");
      }
      if (entry.method == kMethodStored && entry.compressedSize != entry.uncompressedSize) {
        return fail("stored entry has inconsistent sizes");
      }
      m_entries.push_back(entry);
    }
    if (m_entries.size() != wanted.size()) {
      return fail("a file listed in the manifest is missing from the archive");
    }
    return true;
  }

  // Validates the local header and returns the offset of the entry data.
  bool locateData(const CentralEntry &entry, std::uint64_t &dataOffset) {
    std::uint8_t h[kLocalHeaderSize];
    if (!readAt(entry.localHeaderOffset, h, sizeof(h)) || readU32(h) != kLocalSignature) {
      return fail("invalid local file header");
    }
    if ((readU16(h + 6) & (kFlagEncrypted | kFlagStrongEncryption | kFlagMaskedHeaders)) != 0 ||
        readU16(h + 8) != entry.method) {
      return fail("local header does not match the central directory");
    }
    dataOffset = static_cast<std::uint64_t>(entry.localHeaderOffset) + kLocalHeaderSize +
                 readU16(h + 26) + readU16(h + 28);
    if (dataOffset > m_centralOffset ||
        entry.compressedSize > m_centralOffset - dataOffset) {
      return fail("entry data is out of range");
    }
    return true;
  }

  bool extract(const CentralEntry &entry) {
    const ManifestFile &listed = m_manifest.files[entry.manifestIndex];
    std::uint64_t dataOffset = 0;
    if (!locateData(entry, dataOffset)) {
      return false;
    }

    std::string relative = listed.path;
    std::replace(relative.begin(), relative.end(), '/', '\\');
    const std::wstring target = joinPath(m_destRoot, utf8ToWide(relative));
    const std::wstring partial = target + L".part";
    if (!ensureDirectory(parentDirectory(target))) {
      return fail("cannot create output directory");
    }

    UniqueHandle out(::CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!out.valid()) {
      return fail("cannot create output file");
    }
    const bool ok = copyEntryData(entry, listed, dataOffset, out);
    out.reset();
    if (!ok) {
      ::DeleteFileW(partial.c_str());
      return false;
    }
    if (!::MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      ::DeleteFileW(partial.c_str());
      return fail("cannot move extracted file into place");
    }
    return true;
  }

  bool writeOut(const UniqueHandle &out, const std::uint8_t *data, std::size_t length) {
    while (length > 0) {
      DWORD written = 0;
      if (!::WriteFile(out.get(), data, static_cast<DWORD>(length), &written, nullptr) ||
          written == 0) {
        return fail("cannot write output file");
      }
      data += written;
      length -= written;
    }
    return true;
  }

  // Streams the entry into `out`, never writing more than the manifest size,
  // and checks size, CRC-32 and SHA-256.
  bool copyEntryData(const CentralEntry &entry, const ManifestFile &listed,
                     std::uint64_t dataOffset, const UniqueHandle &out) {
    Sha256 hasher;
    if (!hasher.valid()) {
      return fail("SHA-256 is unavailable");
    }
    std::uint64_t produced = 0;
    uLong crc = crc32(0L, Z_NULL, 0);

    // Consumes decoded bytes: bound check, checksums, write.
    const auto emit = [&](const std::uint8_t *data, std::size_t length) {
      if (length > listed.size - produced) {
        return fail("entry is larger than the manifest allows");
      }
      crc = crc32(crc, data, static_cast<uInt>(length));
      hasher.update(data, length);
      produced += length;
      return writeOut(out, data, length);
    };

    std::vector<std::uint8_t> input(kChunkBytes);
    std::uint64_t remaining = entry.compressedSize;
    std::uint64_t position = dataOffset;

    if (entry.method == kMethodStored) {
      while (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kChunkBytes));
        if (!readAt(position, input.data(), want)) {
          return fail("cannot read entry data");
        }
        if (!emit(input.data(), want)) {
          return false;
        }
        position += want;
        remaining -= want;
      }
    } else {
      InflateStream inflater;
      if (!inflater.init()) {
        return fail("zlib initialisation failed");
      }
      z_stream &z = inflater.get();
      std::vector<std::uint8_t> output(kChunkBytes);
      bool streamEnded = false;
      while (!streamEnded) {
        if (z.avail_in == 0 && remaining > 0) {
          const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kChunkBytes));
          if (!readAt(position, input.data(), want)) {
            return fail("cannot read entry data");
          }
          position += want;
          remaining -= want;
          z.next_in = input.data();
          z.avail_in = static_cast<uInt>(want);
        }
        z.next_out = output.data();
        z.avail_out = static_cast<uInt>(output.size());
        const int status = inflate(&z, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
          return fail("corrupt compressed data");
        }
        const std::size_t have = output.size() - z.avail_out;
        if (have > 0 && !emit(output.data(), have)) {
          return false;
        }
        if (status == Z_STREAM_END) {
          streamEnded = true;
        } else if (status == Z_BUF_ERROR) {
          // No progress possible: out of input (truncated) or a stuck stream.
          return fail(z.avail_in == 0 && remaining == 0 ? "compressed data is truncated"
                                                        : "corrupt compressed data");
        }
      }
      if (z.avail_in != 0 || remaining != 0) {
        return fail("unexpected data after the compressed stream");
      }
    }

    if (produced != listed.size) {
      return fail("entry is shorter than the manifest says");
    }
    if (static_cast<std::uint32_t>(crc) != entry.crc) {
      return fail("CRC-32 mismatch");
    }
    if (hasher.finish() != listed.sha256) {
      return fail("SHA-256 mismatch");
    }
    return true;
  }

  const std::wstring &m_zipPath;
  const Manifest &m_manifest;
  const std::wstring &m_destRoot;
  UniqueHandle m_file;
  std::uint64_t m_fileSize = 0;
  std::uint64_t m_centralOffset = 0;
  std::size_t m_entryCount = 0;
  std::vector<std::uint8_t> m_central;
  std::vector<CentralEntry> m_entries;
  std::string m_error;
};

} // namespace

bool extractManifestFiles(const std::wstring &zipPath, const Manifest &manifest,
                          const std::wstring &destRoot, std::string *error) {
  std::string message;
  const bool ok = guarded(
      [&]() {
        ZipExtractor extractor(zipPath, manifest, destRoot);
        const bool result = extractor.run();
        message = extractor.error();
        return result;
      },
      false);
  if (!ok && message.empty()) {
    message = "internal error while extracting the package";
  }
  if (error != nullptr) {
    *error = ok ? std::string() : message;
  }
  return ok;
}

} // namespace fsv::update
