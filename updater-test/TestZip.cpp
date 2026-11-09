// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"
#include "TestKeys.h"

#include "updater-lib/Crypto.h"
#include "updater-lib/FsUtil.h"
#include "updater-lib/ZipReader.h"

#include "vendor/zlib/zlib.h"

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace fsv::update;
using fsv::test::asBytes;
using fsv::test::TempDir;

namespace {

// ------------------------------------------------------------ test ZIP writer

struct ZipEntry {
  std::string name;
  std::string content;
  bool deflate = false;
  std::uint16_t flags = 0;
  bool dataDescriptor = false;
  std::optional<std::uint16_t> methodField;  // written to both headers
  std::optional<std::uint32_t> crc;          // overrides the real CRC-32
  std::optional<std::uint32_t> centralUsize; // overrides the central directory size
  std::optional<std::string> rawData;        // overrides the stored/compressed bytes
};

void put16(std::string &out, std::uint32_t value) {
  out.push_back(static_cast<char>(value & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void put32(std::string &out, std::uint32_t value) {
  put16(out, value & 0xFFFF);
  put16(out, value >> 16);
}

std::uint32_t crcOf(const std::string &data) {
  return static_cast<std::uint32_t>(
      crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.data()),
            static_cast<uInt>(data.size())));
}

std::string deflateRaw(const std::string &data) {
  z_stream z;
  std::memset(&z, 0, sizeof(z));
  CHECK(deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) ==
        Z_OK);
  std::string out(deflateBound(&z, static_cast<uLong>(data.size())) + 64, '\0');
  z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
  z.avail_in = static_cast<uInt>(data.size());
  z.next_out = reinterpret_cast<Bytef *>(out.data());
  z.avail_out = static_cast<uInt>(out.size());
  CHECK(deflate(&z, Z_FINISH) == Z_STREAM_END);
  out.resize(out.size() - z.avail_out);
  deflateEnd(&z);
  return out;
}

std::string buildZip(const std::vector<ZipEntry> &entries, bool zip64Locator = false) {
  std::string out;
  std::string central;
  for (const ZipEntry &entry : entries) {
    const std::string data = entry.rawData ? *entry.rawData
                             : entry.deflate ? deflateRaw(entry.content)
                                             : entry.content;
    const std::uint16_t method =
        entry.methodField ? *entry.methodField : static_cast<std::uint16_t>(entry.deflate ? 8 : 0);
    const std::uint16_t flags = static_cast<std::uint16_t>(entry.flags | (entry.dataDescriptor ? 8 : 0));
    const std::uint32_t crc = entry.crc ? *entry.crc : crcOf(entry.content);
    const std::uint32_t csize = static_cast<std::uint32_t>(data.size());
    const std::uint32_t usize = static_cast<std::uint32_t>(entry.content.size());
    const std::uint32_t offset = static_cast<std::uint32_t>(out.size());

    put32(out, 0x04034b50);
    put16(out, 20);
    put16(out, flags);
    put16(out, method);
    put16(out, 0);
    put16(out, 0x21);
    put32(out, entry.dataDescriptor ? 0 : crc);
    put32(out, entry.dataDescriptor ? 0 : csize);
    put32(out, entry.dataDescriptor ? 0 : usize);
    put16(out, static_cast<std::uint32_t>(entry.name.size()));
    put16(out, 0);
    out += entry.name;
    out += data;
    if (entry.dataDescriptor) {
      put32(out, 0x08074b50);
      put32(out, crc);
      put32(out, csize);
      put32(out, usize);
    }

    put32(central, 0x02014b50);
    put16(central, 20);
    put16(central, 20);
    put16(central, flags);
    put16(central, method);
    put16(central, 0);
    put16(central, 0x21);
    put32(central, crc);
    put32(central, csize);
    put32(central, entry.centralUsize ? *entry.centralUsize : usize);
    put16(central, static_cast<std::uint32_t>(entry.name.size()));
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put32(central, 0);
    put32(central, offset);
    central += entry.name;
  }
  const std::uint32_t centralOffset = static_cast<std::uint32_t>(out.size());
  out += central;
  if (zip64Locator) {
    put32(out, 0x07064b50);
    out.append(16, '\0');
  }
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<std::uint32_t>(entries.size()));
  put16(out, static_cast<std::uint32_t>(entries.size()));
  put32(out, static_cast<std::uint32_t>(central.size()));
  put32(out, centralOffset);
  put16(out, 0);
  return out;
}

void patch16(std::string &zip, std::size_t at, std::uint32_t value) {
  zip[at] = static_cast<char>(value & 0xFF);
  zip[at + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void patch32(std::string &zip, std::size_t at, std::uint32_t value) {
  patch16(zip, at, value & 0xFFFF);
  patch16(zip, at + 2, value >> 16);
}

// EOCD of an archive built without a comment.
std::size_t eocdOf(const std::string &zip) { return zip.size() - 22; }

// ------------------------------------------------------------ test fixtures

std::string pseudoRandom(std::size_t size, std::uint32_t seed) {
  std::string out(size, '\0');
  std::uint32_t state = seed;
  for (char &c : out) {
    state = state * 1664525u + 1013904223u;
    c = static_cast<char>(state >> 24);
  }
  return out;
}

using FileList = std::vector<std::pair<std::string, std::string>>;

Manifest manifestFor(const FileList &files) {
  Manifest manifest;
  manifest.version = Version{0, 3, 0, Channel::Alpha};
  manifest.packageName = "FarSightVNC-v0.3.0a.zip";
  for (const auto &[path, content] : files) {
    ManifestFile file;
    file.path = path;
    file.size = content.size();
    file.sha256 = sha256(asBytes(content));
    manifest.files.push_back(file);
  }
  return manifest;
}

bool writeZip(const std::wstring &path, const std::string &bytes) {
  return writeFileAtomic(path, bytes);
}

std::uintmax_t largestFileUnder(const std::wstring &directory) {
  std::uintmax_t largest = 0;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end;
       it.increment(ec)) {
    std::error_code sizeError;
    if (it->is_regular_file(sizeError)) {
      const std::uintmax_t size = it->file_size(sizeError);
      if (!sizeError && size > largest) {
        largest = size;
      }
    }
  }
  return largest;
}

std::size_t countFilesUnder(const std::wstring &directory) {
  std::size_t count = 0;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end;
       it.increment(ec)) {
    std::error_code typeError;
    if (it->is_regular_file(typeError)) {
      ++count;
    }
  }
  return count;
}

// Runs the extraction into a fresh directory; returns the outcome.
struct Outcome {
  bool ok = false;
  std::string error;
  std::wstring dest;
};

Outcome extract(const TempDir &temp, const std::string &zipBytes, const FileList &files,
                const char *tag) {
  Outcome outcome;
  const std::wstring name = utf8ToWide(tag);
  const std::wstring zipPath = temp.child(name + L".zip");
  outcome.dest = temp.child(L"out-" + name + L"\\root");
  CHECK(writeZip(zipPath, zipBytes));
  outcome.ok = extractManifestFiles(zipPath, manifestFor(files), outcome.dest, &outcome.error);
  return outcome;
}

void expectFailure(const TempDir &temp, const std::string &zipBytes, const FileList &files,
                   const char *tag) {
  const Outcome outcome = extract(temp, zipBytes, files, tag);
  if (outcome.ok) {
    std::printf("  (extraction unexpectedly succeeded: %s)\n", tag);
  }
  CHECK(!outcome.ok);
  CHECK(!outcome.error.empty());
}

// ------------------------------------------------------------ tests

FileList standardFiles() {
  return {{"LICENSE.txt", "License text\nline 2\n"},
          {"x64/tvnviewer.exe", pseudoRandom(300000, 1)},
          {"Win32/tvnviewer.exe", std::string(100000, 'q') + pseudoRandom(5000, 2)},
          {"Win32/sub/readme.txt", "nested file\n"},
          {"README.txt", ""},
          {"LEIAME.txt", ""}};
}

void testSuccessfulExtraction(const TempDir &temp) {
  const FileList files = standardFiles();
  std::vector<ZipEntry> entries;
  entries.push_back({"docs/", ""});                                  // directory entry
  entries.push_back({"license.TXT", files[0].second});               // stored, other case
  ZipEntry viewer64{"x64\\tvnviewer.exe", files[1].second};          // deflated, backslashes
  viewer64.deflate = true;
  entries.push_back(viewer64);
  ZipEntry viewer32{"Win32/tvnviewer.exe", files[2].second};         // deflated + descriptor
  viewer32.deflate = true;
  viewer32.dataDescriptor = true;
  entries.push_back(viewer32);
  ZipEntry nested{"Win32\\sub\\readme.txt", files[3].second};        // stored, backslashes
  entries.push_back(nested);
  entries.push_back({"../evil.txt", "evil"});                        // unlisted: ignored
  entries.push_back({"other.bin", "not in the manifest"});           // unlisted: ignored
  entries.push_back({"README.txt", ""});                             // empty, stored
  ZipEntry emptyDeflated{"LEIAME.txt", ""};                          // empty, deflated
  emptyDeflated.deflate = true;
  entries.push_back(emptyDeflated);

  const Outcome outcome = extract(temp, buildZip(entries), files, "ok");
  if (!outcome.ok) {
    std::printf("  (error: %s)\n", outcome.error.c_str());
  }
  CHECK(outcome.ok);
  CHECK(outcome.error.empty());
  for (const auto &[path, content] : files) {
    std::wstring native = utf8ToWide(path);
    for (wchar_t &c : native) {
      if (c == L'/') {
        c = L'\\';
      }
    }
    const auto data = readFile(joinPath(outcome.dest, native), 10 << 20);
    CHECK(data.has_value() && *data == content);
  }
  // Only the six listed files exist: no .part leftovers, nothing unlisted.
  CHECK(countFilesUnder(temp.child(L"out-ok")) == files.size());
  CHECK(!fileExists(joinPath(outcome.dest, L"other.bin")));
  CHECK(!fileExists(joinPath(parentDirectory(outcome.dest), L"evil.txt")));
  CHECK(!fileExists(temp.child(L"evil.txt")));
  CHECK(!fileExists(temp.child(L"out-ok\\evil.txt")));

  // Extracting again over an existing tree replaces the files.
  CHECK(extractManifestFiles(temp.child(L"ok.zip"), manifestFor(files), outcome.dest, nullptr));
}

void testMissingAndDuplicate(const TempDir &temp) {
  const FileList files = {{"a.txt", "alpha"}, {"b.txt", "bravo"}};

  expectFailure(temp, buildZip({{"a.txt", "alpha"}}), files, "missing");
  expectFailure(temp, buildZip({{"a.txt", "alpha"}, {"b.txt", "bravo"}, {"a.txt", "alpha"}}),
                files, "duplicate");
  expectFailure(temp, buildZip({{"a.txt", "alpha"}, {"b.txt", "bravo"}, {"A.TXT", "alpha"}}),
                files, "duplicate-case");
  expectFailure(temp, buildZip({{"dir\\a.txt", "alpha"}, {"b.txt", "bravo"}}), files,
                "wrong-dir");
  // A duplicated entry that is NOT listed does not matter.
  const Outcome ok = extract(
      temp, buildZip({{"a.txt", "alpha"}, {"x", "1"}, {"x", "2"}, {"b.txt", "bravo"}}), files,
      "unlisted-dup");
  CHECK(ok.ok);
  expectFailure(temp, buildZip({}), files, "empty-archive");
}

void testSizeAndHashMismatch(const TempDir &temp) {
  const FileList files = {{"a.txt", "alpha"}, {"b.txt", "bravo"}};

  ZipEntry sizeMismatch{"a.txt", "alpha"};
  sizeMismatch.centralUsize = 6u;
  expectFailure(temp, buildZip({sizeMismatch, {"b.txt", "bravo"}}), files, "central-size");

  ZipEntry sizeMismatchDeflated{"a.txt", "alpha"};
  sizeMismatchDeflated.deflate = true;
  sizeMismatchDeflated.centralUsize = 4u;
  expectFailure(temp, buildZip({sizeMismatchDeflated, {"b.txt", "bravo"}}), files,
                "central-size-deflated");

  // Same length, different bytes, valid CRC-32: only the SHA-256 catches it.
  expectFailure(temp, buildZip({{"a.txt", "ALPHA"}, {"b.txt", "bravo"}}), files, "sha-mismatch");
  ZipEntry shaMismatchDeflated{"a.txt", "ALPHA"};
  shaMismatchDeflated.deflate = true;
  expectFailure(temp, buildZip({shaMismatchDeflated, {"b.txt", "bravo"}}), files,
                "sha-mismatch-deflated");

  // Right bytes, wrong CRC-32 in the archive.
  ZipEntry badCrc{"a.txt", "alpha"};
  badCrc.crc = crcOf("alpha") ^ 1u;
  expectFailure(temp, buildZip({badCrc, {"b.txt", "bravo"}}), files, "crc-stored");
  ZipEntry badCrcDeflated{"a.txt", "alpha"};
  badCrcDeflated.deflate = true;
  badCrcDeflated.crc = 0u;
  expectFailure(temp, buildZip({badCrcDeflated, {"b.txt", "bravo"}}), files, "crc-deflated");
}

void testUnsupportedFeatures(const TempDir &temp) {
  const FileList files = {{"a.txt", "alpha"}, {"b.txt", "bravo"}};

  ZipEntry encrypted{"a.txt", "alpha"};
  encrypted.flags = 0x0001;
  expectFailure(temp, buildZip({encrypted, {"b.txt", "bravo"}}), files, "encrypted");

  ZipEntry strong{"a.txt", "alpha"};
  strong.flags = 0x0040;
  expectFailure(temp, buildZip({strong, {"b.txt", "bravo"}}), files, "strong-encryption");

  ZipEntry method12{"a.txt", "alpha"};
  method12.methodField = std::uint16_t{12}; // bzip2
  expectFailure(temp, buildZip({method12, {"b.txt", "bravo"}}), files, "method12");

  ZipEntry method9{"a.txt", "alpha"};
  method9.methodField = std::uint16_t{9}; // deflate64
  expectFailure(temp, buildZip({method9, {"b.txt", "bravo"}}), files, "method9");

  // ZIP64: markers in the EOCD, and a ZIP64 locator before it.
  const std::string valid = buildZip({{"a.txt", "alpha"}, {"b.txt", "bravo"}});
  {
    std::string zip = valid;
    patch32(zip, eocdOf(zip) + 16, 0xFFFFFFFFu);
    expectFailure(temp, zip, files, "zip64-offset");
  }
  {
    std::string zip = valid;
    patch32(zip, eocdOf(zip) + 12, 0xFFFFFFFFu);
    expectFailure(temp, zip, files, "zip64-size");
  }
  {
    std::string zip = valid;
    patch16(zip, eocdOf(zip) + 8, 0xFFFF);
    patch16(zip, eocdOf(zip) + 10, 0xFFFF);
    expectFailure(temp, zip, files, "zip64-count");
  }
  expectFailure(temp, buildZip({{"a.txt", "alpha"}, {"b.txt", "bravo"}}, true), files,
                "zip64-locator");
  {
    // Multi-disk markers.
    std::string zip = valid;
    patch16(zip, eocdOf(zip) + 4, 1);
    expectFailure(temp, zip, files, "multi-disk");
    zip = valid;
    patch16(zip, eocdOf(zip) + 6, 1);
    expectFailure(temp, zip, files, "multi-disk-cd");
    zip = valid;
    patch16(zip, eocdOf(zip) + 8, 1);
    expectFailure(temp, zip, files, "multi-disk-count");
  }
  {
    // ZIP64 sentinel in an entry's central record.
    std::string zip = valid;
    const std::size_t central = zip.find("PK\x01\x02");
    CHECK(central != std::string::npos);
    patch32(zip, central + 20, 0xFFFFFFFFu);
    expectFailure(temp, zip, files, "zip64-entry");
  }
}

void testCorruptArchives(const TempDir &temp) {
  const FileList files = {{"a.txt", "alpha"}, {"b.txt", std::string(5000, 'b')}};
  ZipEntry b{"b.txt", files[1].second};
  b.deflate = true;
  const std::string valid = buildZip({{"a.txt", "alpha"}, b});
  CHECK(extract(temp, valid, files, "corrupt-baseline").ok);

  // Truncation at every interesting place.
  expectFailure(temp, valid.substr(0, valid.size() - 10), files, "truncated-eocd");
  expectFailure(temp, valid.substr(0, valid.size() / 2), files, "truncated-half");
  expectFailure(temp, valid.substr(0, 21), files, "truncated-tiny");
  expectFailure(temp, std::string(), files, "empty-file");
  expectFailure(temp, std::string(100, 'x'), files, "garbage-100");
  expectFailure(temp, pseudoRandom(70000, 3), files, "garbage-70000");
  expectFailure(temp, std::string("PK\x05\x06"), files, "eocd-signature-only");

  // EOCD fields out of range.
  const std::size_t eocd = eocdOf(valid);
  {
    std::string zip = valid;
    patch32(zip, eocd + 16, 0x00FFFFFFu); // central directory offset beyond the file
    expectFailure(temp, zip, files, "cd-offset-range");
  }
  {
    std::string zip = valid;
    patch32(zip, eocd + 12, 0x00FFFFFFu); // central directory size beyond the file
    expectFailure(temp, zip, files, "cd-size-range");
  }
  {
    std::string zip = valid;
    patch32(zip, eocd + 12, 0x7FFFFFFFu);
    patch32(zip, eocd + 16, 0x7FFFFFFFu); // sum overflows 32 bits
    expectFailure(temp, zip, files, "cd-overflow");
  }
  {
    std::string zip = valid;
    patch16(zip, eocd + 10, 500);
    patch16(zip, eocd + 8, 500); // more entries than the directory holds
    expectFailure(temp, zip, files, "cd-count");
  }
  {
    std::string zip = valid;
    patch16(zip, eocd + 20, 40); // comment length claims bytes that are not there
    expectFailure(temp, zip, files, "comment-length");
  }
  {
    // Central directory signature damaged.
    std::string zip = valid;
    zip[zip.find("PK\x01\x02")] = 'X';
    expectFailure(temp, zip, files, "cd-signature");
  }
  {
    // Local header signature damaged.
    std::string zip = valid;
    zip[0] = 'X';
    expectFailure(temp, zip, files, "local-signature");
  }
  {
    // Central record points its local header past the data area.
    std::string zip = valid;
    const std::size_t central = zip.find("PK\x01\x02");
    patch32(zip, central + 42, static_cast<std::uint32_t>(zip.size() - 5));
    expectFailure(temp, zip, files, "local-offset-range");
  }
  {
    // Local header claims a name so long that the data would run into the directory.
    std::string zip = valid;
    patch16(zip, 26, 60000);
    expectFailure(temp, zip, files, "local-name-range");
  }
  {
    // Local method differs from the central directory.
    std::string zip = valid;
    patch16(zip, 8, 8);
    expectFailure(temp, zip, files, "local-method");
  }

  // Deflate data problems.
  const std::string compressed = deflateRaw(files[1].second);
  {
    ZipEntry truncated{"b.txt", files[1].second};
    truncated.rawData = compressed.substr(0, compressed.size() / 2);
    truncated.methodField = std::uint16_t{8};
    expectFailure(temp, buildZip({{"a.txt", "alpha"}, truncated}), files, "deflate-truncated");
  }
  {
    ZipEntry junk{"b.txt", files[1].second};
    junk.rawData = pseudoRandom(200, 9);
    junk.methodField = std::uint16_t{8};
    expectFailure(temp, buildZip({{"a.txt", "alpha"}, junk}), files, "deflate-garbage");
  }
  {
    ZipEntry trailing{"b.txt", files[1].second};
    trailing.rawData = compressed + "JUNK!";
    trailing.methodField = std::uint16_t{8};
    expectFailure(temp, buildZip({{"a.txt", "alpha"}, trailing}), files, "deflate-trailing");
  }
  {
    // Compressed data shorter than the manifest size (stream ends early).
    ZipEntry shortEntry{"b.txt", std::string(100, 'b')};
    shortEntry.deflate = true;
    shortEntry.centralUsize = 5000u;
    expectFailure(temp, buildZip({{"a.txt", "alpha"}, shortEntry}), files, "deflate-short");
  }
}

void testZipBomb(const TempDir &temp) {
  const std::string bomb(10u << 20, '\0'); // 10 MiB of zeros, ~10 KiB deflated
  const FileList files = {{"x64/payload.bin", std::string(1024, '\0')}, {"ok.txt", "fine"}};

  ZipEntry entry{"x64/payload.bin", bomb};
  entry.deflate = true;
  entry.centralUsize = 1024u; // the archive lies about the size, as a bomb would
  const std::string zip = buildZip({entry, {"ok.txt", "fine"}});
  CHECK(zip.size() < 100000);

  const Outcome outcome = extract(temp, zip, files, "bomb");
  CHECK(!outcome.ok);
  CHECK(!outcome.error.empty());
  CHECK(largestFileUnder(temp.child(L"out-bomb")) <= 1024);
  CHECK(!fileExists(joinPath(outcome.dest, L"x64\\payload.bin")));
  CHECK(!fileExists(joinPath(outcome.dest, L"x64\\payload.bin.part")));

  // Same with a stored entry whose real data is larger than what it declares.
  ZipEntry stored{"x64/payload.bin", std::string(4096, 'z')};
  stored.centralUsize = 1024u;
  const Outcome storedOutcome = extract(temp, buildZip({stored, {"ok.txt", "fine"}}), files, "bomb2");
  CHECK(!storedOutcome.ok);
  CHECK(largestFileUnder(temp.child(L"out-bomb2")) <= 1024);
}

void testBadArguments(const TempDir &temp) {
  const FileList files = {{"a.txt", "alpha"}};
  const std::string zip = buildZip({{"a.txt", "alpha"}});
  const std::wstring zipPath = temp.child(L"args.zip");
  CHECK(writeZip(zipPath, zip));
  std::string error;
  CHECK(!extractManifestFiles(temp.child(L"nonexistent.zip"), manifestFor(files),
                              temp.child(L"out-args1"), &error));
  CHECK(!error.empty());
  CHECK(!extractManifestFiles(zipPath, manifestFor(files), L"", &error));
  CHECK(!extractManifestFiles(zipPath, Manifest{}, temp.child(L"out-args2"), &error));
  // The error pointer is optional.
  CHECK(!extractManifestFiles(temp.child(L"nonexistent.zip"), manifestFor(files),
                              temp.child(L"out-args3"), nullptr));
  CHECK(extractManifestFiles(zipPath, manifestFor(files), temp.child(L"out-args4"), &error));
  CHECK(error.empty());
}

} // namespace

namespace fsv::test {

std::string buildSimpleZip(const std::vector<std::pair<std::string, std::string>> &files) {
  std::vector<ZipEntry> entries;
  for (const auto &[name, content] : files) {
    ZipEntry entry;
    entry.name = name;
    entry.content = content;
    entry.deflate = true;
    entries.push_back(std::move(entry));
  }
  return buildZip(entries);
}

} // namespace fsv::test

void runZipTests() {
  fsv::test::beginSuite("zip");
  const TempDir temp;
  CHECK(!temp.path().empty());
  if (temp.path().empty()) {
    return;
  }
  testSuccessfulExtraction(temp);
  testMissingAndDuplicate(temp);
  testSizeAndHashMismatch(temp);
  testUnsupportedFeatures(temp);
  testCorruptArchives(temp);
  testZipBomb(temp);
  testBadArguments(temp);
}
