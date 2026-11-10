// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"
#include "TestKeys.h"

#include "updater-lib/FsUtil.h"
#include "updater-lib/Manifest.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace fsv::update;
using fsv::test::asBytes;
using fsv::test::TestKey;

namespace {

const std::string kHashA(64, 'a');
const std::string kHashB(64, 'b');

std::string packageLine(const std::string &name = "FarSightVNC-v0.3.0a.zip",
                        const std::string &size = "1234", const std::string &hash = kHashA) {
  return "package " + name + " " + size + " " + hash + "\n";
}

std::string fileLine(const std::string &path, const std::string &size = "10",
                     const std::string &hash = kHashB) {
  return "file " + path + " " + size + " " + hash + "\n";
}

const std::string kHeader = "FarSightVNC-Update-Manifest 1\n";

std::string validManifest() {
  return kHeader + "version 0.3.0a\n" + packageLine() + fileLine("fsvncviewer.exe", "100") +
         fileLine("fsvncupdater.exe", "0") + fileLine("LICENSE.txt");
}

void expectRejected(const std::string &text, const char *what) {
  if (parseManifest(text).has_value()) {
    std::printf("  (accepted invalid manifest: %s)\n", what);
  }
  CHECK(!parseManifest(text).has_value());
}

void testSafeRelativePath() {
  const char *const good[] = {"LICENSE.txt",       "fsvncviewer.exe",   "fsvncupdater.exe",
                              "a",                 "a/b/c/d",           "a-b_c.d",
                              "x64/COM10.dll",     "x64/console.txt",   "x64/null",
                              "README.txt",        "a.b.c"};
  for (const char *path : good) {
    if (!isSafeRelativePath(path)) {
      std::printf("  (valid path rejected: %s)\n", path);
    }
    CHECK(isSafeRelativePath(path));
  }

  const std::string longest(200, 'a');
  CHECK(isSafeRelativePath(longest));
  const std::string bad[] = {"",
                             ".",
                             "..",
                             "../x",
                             "x/../y",
                             "x/./y",
                             "/x",
                             "C:/x",
                             "x\\y",
                             "\\x",
                             "a:b",
                             "CON",
                             "con.txt",
                             "x64/NUL",
                             "LPT9.dat",
                             "prn",
                             "aux.x.y",
                             "COM1",
                             "com9.txt",
                             "x.",
                             ".x",
                             "x/.hidden",
                             "a//b",
                             "a/",
                             "/",
                             "a/b/c/d/e",
                             std::string(201, 'a'),
                             "x64/tvn viewer.exe",
                             "x64/t\xC3\xA9st.exe",
                             std::string("a\0b", 3),
                             "a\tb",
                             "a*b",
                             "a?b",
                             "a\"b",
                             "a<b",
                             "a|b",
                             "~x",
                             "a b"};
  for (const std::string &path : bad) {
    if (isSafeRelativePath(path)) {
      std::printf("  (unsafe path accepted: %s)\n", path.c_str());
    }
    CHECK(!isSafeRelativePath(path));
  }
}

void testParseManifestValid() {
  const auto parsed = parseManifest(validManifest());
  CHECK(parsed.has_value());
  if (!parsed) {
    return;
  }
  CHECK(parsed->version.toString() == "0.3.0a");
  CHECK(parsed->packageName == "FarSightVNC-v0.3.0a.zip");
  CHECK(parsed->packageSize == 1234);
  CHECK(toHex(parsed->packageSha256) == kHashA);
  CHECK(parsed->files.size() == 3);
  if (parsed->files.size() == 3) {
    CHECK(parsed->files[0].path == "fsvncviewer.exe");
    CHECK(parsed->files[0].size == 100);
    CHECK(toHex(parsed->files[0].sha256) == kHashB);
    CHECK(parsed->files[1].size == 0);
    CHECK(parsed->files[2].path == "LICENSE.txt");
  }

  // CRLF, no final newline, upper-case hex, case-insensitive ".ZIP".
  CHECK(parseManifest("FarSightVNC-Update-Manifest 1\r\nversion 1.0.0\r\npackage p.ZIP 1 " +
                      kHashA + "\r\nfile a.txt 1 " + std::string(64, 'F') + "\r\n")
            .has_value());
  CHECK(parseManifest("FarSightVNC-Update-Manifest 1\nversion 1.0.0\npackage p.zip 1 " + kHashA +
                      "\nfile a.txt 1 " + kHashB)
            .has_value());
  CHECK(parseManifest("FarSightVNC-Update-Manifest 1\nversion 1.0.0b\npackage p.zip 0 " + kHashA +
                      "\nfile a.txt 0 " + kHashB + "\n")
            .has_value());

  // Limits that are still allowed.
  CHECK(parseManifest(kHeader + "version 1.0.0\n" + packageLine("p.zip", "134217728") +
                      fileLine("a", "67108864"))
            .has_value());
  std::string maxFiles = kHeader + "version 1.0.0\n" + packageLine();
  for (std::size_t i = 0; i < kMaxManifestFiles; ++i) {
    maxFiles += fileLine("f" + std::to_string(i) + ".bin");
  }
  CHECK(parseManifest(maxFiles).has_value());
}

void testParseManifestRejects() {
  const std::string v = "version 0.3.0a\n";
  const std::string pkg = packageLine();
  const std::string file = fileLine("a.txt");

  expectRejected("", "empty");
  expectRejected(kHeader, "header only");
  expectRejected(kHeader + v + pkg, "no file line");
  expectRejected("FarSightVNC-Update-Manifest 2\n" + v + pkg + file, "wrong format version");
  expectRejected("farsightvnc-update-manifest 1\n" + v + pkg + file, "header case");
  expectRejected("FarSightVNC-Update-Manifest 1 \n" + v + pkg + file, "trailing space on header");
  expectRejected(v + pkg + file, "missing header");
  expectRejected("\n" + kHeader + v + pkg + file, "leading blank line");
  expectRejected(kHeader + "\n" + v + pkg + file, "blank line after header");
  expectRejected(kHeader + v + pkg + file + "\n", "blank line at the end");
  expectRejected(kHeader + v + "\n" + pkg + file, "blank line in the middle");
  expectRejected("\xEF\xBB\xBF" + kHeader + v + pkg + file, "BOM");

  // Order and multiplicity.
  expectRejected(kHeader + pkg + v + file, "package before version");
  expectRejected(kHeader + v + file + pkg, "file before package");
  expectRejected(kHeader + v + v + pkg + file, "two version lines");
  expectRejected(kHeader + v + pkg + pkg + file, "two package lines");
  expectRejected(kHeader + v + pkg + file + v, "version after files");
  expectRejected(kHeader + v + pkg + file + pkg, "package after files");
  expectRejected(kHeader + pkg + file, "missing version");
  expectRejected(kHeader + v + file, "missing package");
  expectRejected(kHeader + v + pkg + file + "other x\n", "unknown keyword");
  expectRejected(kHeader + v + pkg + file + "# comment\n", "comment line");

  // Version.
  expectRejected(kHeader + "version v0.3.0a\n" + pkg + file, "leading v");
  expectRejected(kHeader + "version 0.3\n" + pkg + file, "two components");
  expectRejected(kHeader + "version 0.3.0c\n" + pkg + file, "unknown suffix");
  expectRejected(kHeader + "version 0.3.0 \n" + pkg + file, "trailing space");
  expectRejected(kHeader + "version  0.3.0\n" + pkg + file, "double space");
  expectRejected(kHeader + "version 03.0.0\n" + pkg + file, "leading zero version");
  expectRejected(kHeader + "version\n" + pkg + file, "missing version value");
  expectRejected(kHeader + "version 1.0.0 extra\n" + pkg + file, "extra field");

  // Package line.
  expectRejected(kHeader + v + packageLine("../p.zip") + file, "package traversal");
  expectRejected(kHeader + v + packageLine("a/p.zip") + file, "package with a directory");
  expectRejected(kHeader + v + packageLine("p.zip.txt") + file, "package not a zip");
  expectRejected(kHeader + v + packageLine(".zip") + file, "package without a stem");
  expectRejected(kHeader + v + packageLine("CON.zip") + file, "package device name");
  expectRejected(kHeader + v + packageLine("p q.zip") + file, "package with a space");
  expectRejected(kHeader + v + packageLine("p.zip", "01") + file, "package size leading zero");
  expectRejected(kHeader + v + packageLine("p.zip", "-1") + file, "package negative size");
  expectRejected(kHeader + v + packageLine("p.zip", "+1") + file, "package plus size");
  expectRejected(kHeader + v + packageLine("p.zip", "1.5") + file, "package fractional size");
  expectRejected(kHeader + v + packageLine("p.zip", "") + file, "package empty size");
  expectRejected(kHeader + v + packageLine("p.zip", "0x10") + file, "package hex size");
  expectRejected(kHeader + v + packageLine("p.zip", "134217729") + file, "package too big");
  expectRejected(kHeader + v + packageLine("p.zip", "99999999999999999999999") + file,
                 "package size overflow");
  expectRejected(kHeader + v + packageLine("p.zip", "1", std::string(63, 'a')) + file,
                 "short package hash");
  expectRejected(kHeader + v + packageLine("p.zip", "1", std::string(65, 'a')) + file,
                 "long package hash");
  expectRejected(kHeader + v + packageLine("p.zip", "1", std::string(63, 'a') + "g") + file,
                 "non-hex package hash");
  expectRejected(kHeader + v + "package p.zip 1\n" + file, "package without hash");
  expectRejected(kHeader + v + "package p.zip 1 " + kHashA + " x\n" + file,
                 "package extra field");

  // File lines.
  expectRejected(kHeader + v + pkg + fileLine("../a"), "file traversal");
  expectRejected(kHeader + v + pkg + fileLine("/a"), "absolute file");
  expectRejected(kHeader + v + pkg + fileLine("a\\b"), "backslash file");
  expectRejected(kHeader + v + pkg + fileLine("NUL.txt"), "file device name");
  expectRejected(kHeader + v + pkg + fileLine("a/b/c/d/e"), "five segments");
  expectRejected(kHeader + v + pkg + fileLine("a.txt", "01"), "file size leading zero");
  expectRejected(kHeader + v + pkg + fileLine("a.txt", "67108865"), "file too big");
  expectRejected(kHeader + v + pkg + fileLine("a.txt", "-5"), "file negative size");
  expectRejected(kHeader + v + pkg + fileLine("a.txt", "10", std::string(63, 'b')),
                 "short file hash");
  expectRejected(kHeader + v + pkg + "file a.txt 10\n", "file without hash");
  expectRejected(kHeader + v + pkg + "file  a.txt 10 " + kHashB + "\n", "double space in file");
  expectRejected(kHeader + v + pkg + "file a.txt 10 " + kHashB + " \n", "trailing space in file");
  expectRejected(kHeader + v + pkg + "file a.txt\t10 " + kHashB + "\n", "tab separator");
  expectRejected(kHeader + v + pkg + "File a.txt 10 " + kHashB + "\n", "keyword case");
  expectRejected(kHeader + v + pkg + fileLine("a.txt") + fileLine("a.txt"), "duplicate file");
  expectRejected(kHeader + v + pkg + fileLine("a.txt") + fileLine("A.TXT"),
                 "case-insensitive duplicate");
  expectRejected(kHeader + v + pkg + fileLine("x64/a.txt") + fileLine("X64/A.txt"),
                 "case-insensitive duplicate in a directory");

  std::string tooMany = kHeader + v + pkg;
  for (std::size_t i = 0; i <= kMaxManifestFiles; ++i) {
    tooMany += fileLine("f" + std::to_string(i) + ".bin");
  }
  expectRejected(tooMany, "too many files");

  // Bytes that must never appear.
  expectRejected(kHeader + v + pkg + std::string("file a.txt 10 ") + kHashB + std::string("\0\n", 2),
                 "NUL byte");
  expectRejected(std::string(kHeader + v + pkg + file).insert(5, 1, '\0'), "NUL in header");
  expectRejected(kHeader + v + pkg + "file a.txt 10 " + kHashB + "\r\r\n", "double CR");
  expectRejected(kHeader + v + pkg + "file a.txt 10 " + kHashB + "\rfile b 1 " + kHashB + "\n",
                 "bare CR");
  expectRejected(kHeader + v + pkg + "file \xC3\xA9.txt 10 " + kHashB + "\n", "non-ASCII path");
  expectRejected(kHeader + v + pkg + "file a.txt 10 " + kHashB + "\x01\n", "control character");

  // Size cap of the signed document.
  expectRejected(std::string(kMaxManifestBytes + 1, 'x'), "oversized text");
}

void testVerifyAndParse() {
  const TestKey key;
  const TestKey other;
  CHECK(key.valid() && other.valid());
  if (!key.valid() || !other.valid()) {
    return;
  }
  const std::string text = validManifest();
  const std::vector<std::uint8_t> signature = key.sign(text);
  CHECK(signature.size() == kSignatureBytes);

  const TrustedKey trusted[] = {{1, key.publicBlob()}};
  ManifestError error = ManifestError::Malformed;
  auto parsed = verifyAndParseManifest(asBytes(text), signature, trusted, &error);
  CHECK(parsed.has_value());
  CHECK(error == ManifestError::None);
  CHECK(parsed && parsed->files.size() == 3);
  // The error out-parameter is optional.
  CHECK(verifyAndParseManifest(asBytes(text), signature, trusted).has_value());

  // One tampered byte of the manifest.
  std::string tampered = text;
  tampered[text.find("100")] = '9';
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(tampered), signature, trusted, &error).has_value());
  CHECK(error == ManifestError::BadSignature);

  // One tampered byte of the signature.
  std::vector<std::uint8_t> badSignature = signature;
  badSignature[10] ^= 0x01;
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(text), badSignature, trusted, &error).has_value());
  CHECK(error == ManifestError::BadSignature);

  // Wrong signature length (63, 65, empty).
  for (const std::size_t length : {std::size_t{0}, std::size_t{63}, std::size_t{65}}) {
    std::vector<std::uint8_t> wrongLength(length, 0x11);
    if (length <= signature.size()) {
      wrongLength.assign(signature.begin(), signature.begin() + static_cast<std::ptrdiff_t>(length));
    }
    error = ManifestError::None;
    CHECK(!verifyAndParseManifest(asBytes(text), wrongLength, trusted, &error).has_value());
    CHECK(error == ManifestError::BadSignature);
  }

  // Signed by an unrelated key.
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(text), other.sign(text), trusted, &error).has_value());
  CHECK(error == ManifestError::BadSignature);

  // No keys at all.
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(text), signature, {}, &error).has_value());
  CHECK(error == ManifestError::BadSignature);

  // Valid with the second key of the list.
  const TrustedKey two[] = {{1, other.publicBlob()}, {2, key.publicBlob()}};
  error = ManifestError::None;
  CHECK(verifyAndParseManifest(asBytes(text), signature, two, &error).has_value());
  CHECK(error == ManifestError::None);

  // Oversize manifests are rejected before any signature work.
  const std::string huge(kMaxManifestBytes + 1, 'x');
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(huge), key.sign(huge), trusted, &error).has_value());
  CHECK(error == ManifestError::TooLarge);

  // Validly signed but malformed text: Malformed, not BadSignature.
  const std::string malformed = "this is not a manifest\n";
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(malformed), key.sign(malformed), trusted, &error)
             .has_value());
  CHECK(error == ManifestError::Malformed);
  const std::string traversal = kHeader + "version 1.0.0\n" + packageLine() + fileLine("../evil");
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(traversal), key.sign(traversal), trusted, &error)
             .has_value());
  CHECK(error == ManifestError::Malformed);

  // The compiled-in production keys never accept a manifest signed with a test key.
  error = ManifestError::None;
  CHECK(!verifyAndParseManifest(asBytes(text), signature, productionKeys(), &error).has_value());
  CHECK(error == ManifestError::BadSignature);
  CHECK(!productionKeys().empty());
}

std::string environmentVariable(const char *name) {
  char *value = nullptr;
  std::size_t length = 0;
  std::string result;
  if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
    result = value;
  }
  std::free(value);
  return result;
}

// Optional: verifies the output of tools/release/New-UpdateManifest.ps1 (set
// up by tools/release/Test-ReleaseScripts.ps1) with the C++ verifier.
void testPowerShellManifest() {
  const std::string directory = environmentVariable("FSV_PS_MANIFEST_DIR");
  if (directory.empty()) {
    fsv::test::note("FSV_PS_MANIFEST_DIR not set: skipping PowerShell manifest interop check");
    return;
  }
  const std::wstring root = utf8ToWide(directory);
  const auto manifestBytes = readFile(joinPath(root, L"farsightvnc-update.manifest"), 1 << 20);
  const auto signature = readFile(joinPath(root, L"farsightvnc-update.manifest.sig"), 1 << 20);
  const auto blob = readFile(joinPath(root, L"update-key-test.public.blob"), 1 << 20);
  CHECK(manifestBytes.has_value());
  CHECK(signature.has_value());
  CHECK(blob.has_value());
  if (!manifestBytes || !signature || !blob) {
    return;
  }
  CHECK(blob->size() == 72);
  const std::vector<std::uint8_t> blobBytes(blob->begin(), blob->end());
  const TrustedKey trusted[] = {{1, blobBytes}};
  ManifestError error = ManifestError::None;
  const auto parsed =
      verifyAndParseManifest(asBytes(*manifestBytes), asBytes(*signature), trusted, &error);
  CHECK(parsed.has_value());
  if (!parsed) {
    std::printf("  (manifest error code %d)\n", static_cast<int>(error));
    return;
  }
  const std::string expected = environmentVariable("FSV_PS_EXPECT_VERSION");
  if (!expected.empty()) {
    CHECK(parsed->version.toString() == expected);
    CHECK(parsed->packageName == "FarSightVNC-v" + expected + ".zip");
  }
  CHECK(!parsed->files.empty());
  bool hasLicense = false;
  bool hasViewer = false;
  for (const ManifestFile &file : parsed->files) {
    hasLicense = hasLicense || file.path == "LICENSE.txt";
    hasViewer = hasViewer || file.path == "fsvncviewer.exe";
  }
  CHECK(hasLicense);
  CHECK(hasViewer);
}

} // namespace

void runManifestTests() {
  fsv::test::beginSuite("manifest");
  testSafeRelativePath();
  testParseManifestValid();
  testParseManifestRejects();
  testVerifyAndParse();
  testPowerShellManifest();
}
