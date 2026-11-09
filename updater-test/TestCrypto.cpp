// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"
#include "TestKeys.h"

#include "updater-lib/Crypto.h"
#include "updater-lib/FsUtil.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace fsv::update;
using fsv::test::asBytes;
using fsv::test::TempDir;
using fsv::test::TestKey;

namespace {

std::string hashOf(const std::string &text) { return toHex(sha256(asBytes(text))); }

void testSha256Vectors() {
  CHECK(hashOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hashOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  // One million 'a' fed incrementally in uneven chunks.
  Sha256 hasher;
  CHECK(hasher.valid());
  const std::string chunk(1000, 'a');
  for (int i = 0; i < 1000; ++i) {
    hasher.update(chunk.data(), chunk.size());
  }
  const Sha256Digest digest = hasher.finish();
  CHECK(toHex(digest) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  CHECK(!hasher.valid()); // single use

  Sha256 uneven;
  const std::string text(1000000, 'a');
  std::size_t offset = 0;
  std::size_t step = 1;
  while (offset < text.size()) {
    const std::size_t piece = std::min(step, text.size() - offset);
    uneven.update(text.data() + offset, piece);
    offset += piece;
    step = step * 3 + 1;
  }
  CHECK(toHex(uneven.finish()) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

void testSha256File() {
  TempDir dir;
  CHECK(!dir.path().empty());

  // Empty file.
  const std::wstring emptyPath = dir.child(L"empty.bin");
  CHECK(writeFileAtomic(emptyPath, ""));
  std::uint64_t size = 99;
  const auto emptyHash = sha256File(emptyPath, &size);
  CHECK(emptyHash.has_value());
  CHECK(size == 0);
  CHECK(emptyHash && toHex(*emptyHash) ==
                         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  // Multi-chunk file (more than 64 KiB, not a multiple of the chunk size).
  std::string content(200003, '\0');
  for (std::size_t i = 0; i < content.size(); ++i) {
    content[i] = static_cast<char>((i * 131 + 7) & 0xFF);
  }
  const std::wstring bigPath = dir.child(L"big.bin");
  CHECK(writeFileAtomic(bigPath, content));
  const auto fileHash = sha256File(bigPath, &size);
  CHECK(fileHash.has_value());
  CHECK(size == content.size());
  CHECK(fileHash && *fileHash == sha256(asBytes(content)));
  CHECK(sha256File(bigPath).has_value()); // sizeOut is optional

  // Missing file and a directory are I/O errors.
  CHECK(!sha256File(dir.child(L"missing.bin")).has_value());
  CHECK(!sha256File(dir.path()).has_value());
}

void testHex() {
  Sha256Digest digest{};
  for (std::size_t i = 0; i < digest.size(); ++i) {
    digest[i] = static_cast<std::uint8_t>(i * 7 + 3);
  }
  const std::string hex = toHex(digest);
  CHECK(hex.size() == 64);
  CHECK(hex.substr(0, 6) == "030a11");
  const auto parsed = parseSha256Hex(hex);
  CHECK(parsed.has_value());
  CHECK(parsed && *parsed == digest);

  std::string upper = hex;
  for (char &c : upper) {
    if (c >= 'a' && c <= 'f') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  const auto parsedUpper = parseSha256Hex(upper);
  CHECK(parsedUpper.has_value());
  CHECK(parsedUpper && *parsedUpper == digest);
  CHECK(toHex(*parsedUpper) == hex); // output is always lower-case

  CHECK(!parseSha256Hex("").has_value());
  CHECK(!parseSha256Hex(hex.substr(1)).has_value());     // 63 characters
  CHECK(!parseSha256Hex(hex + "0").has_value());         // 65 characters
  std::string bad = hex;
  bad[10] = 'g';
  CHECK(!parseSha256Hex(bad).has_value());
  bad = hex;
  bad[63] = ' ';
  CHECK(!parseSha256Hex(bad).has_value());
  CHECK(!parseSha256Hex("0x" + hex.substr(2)).has_value());
}

void testEcdsa() {
  TestKey key;
  TestKey other;
  CHECK(key.valid());
  CHECK(other.valid());
  CHECK(key.publicBlob().size() == 72);
  CHECK(key.publicBlob() != other.publicBlob());

  const std::string message = "FarSightVNC-Update-Manifest 1\nversion 0.3.0a\n";
  const std::vector<std::uint8_t> signature = key.sign(message);
  CHECK(signature.size() == 64);

  const auto blob = std::span<const std::uint8_t>(key.publicBlob());
  CHECK(verifyEcdsaP256(blob, asBytes(message), signature));

  // Signatures are randomized; a second one verifies too.
  const std::vector<std::uint8_t> second = key.sign(message);
  CHECK(second != signature);
  CHECK(verifyEcdsaP256(blob, asBytes(message), second));

  // Empty message.
  const std::vector<std::uint8_t> emptySignature = key.sign(std::string());
  CHECK(verifyEcdsaP256(blob, asBytes(std::string()), emptySignature));
  CHECK(!verifyEcdsaP256(blob, asBytes(message), emptySignature));

  // Flipped data bit, at the first, a middle and the last byte.
  for (const std::size_t index : {std::size_t{0}, message.size() / 2, message.size() - 1}) {
    std::string tampered = message;
    tampered[index] = static_cast<char>(tampered[index] ^ 0x01);
    CHECK(!verifyEcdsaP256(blob, asBytes(tampered), signature));
  }
  CHECK(!verifyEcdsaP256(blob, asBytes(message + "x"), signature));
  CHECK(!verifyEcdsaP256(blob, asBytes(message.substr(1)), signature));

  // Flipped signature bit (r and s halves).
  for (const std::size_t index : {std::size_t{0}, std::size_t{31}, std::size_t{32}, std::size_t{63}}) {
    std::vector<std::uint8_t> tampered = signature;
    tampered[index] = static_cast<std::uint8_t>(tampered[index] ^ 0x80);
    CHECK(!verifyEcdsaP256(blob, asBytes(message), tampered));
  }

  // Wrong key.
  CHECK(!verifyEcdsaP256(other.publicBlob(), asBytes(message), signature));

  // Truncated / extended / empty signature.
  CHECK(!verifyEcdsaP256(blob, asBytes(message),
                         std::span<const std::uint8_t>(signature).first(63)));
  CHECK(!verifyEcdsaP256(blob, asBytes(message),
                         std::span<const std::uint8_t>(signature).first(32)));
  CHECK(!verifyEcdsaP256(blob, asBytes(message), std::span<const std::uint8_t>()));
  std::vector<std::uint8_t> longer = signature;
  longer.push_back(0);
  CHECK(!verifyEcdsaP256(blob, asBytes(message), longer));
  CHECK(!verifyEcdsaP256(blob, asBytes(message), std::vector<std::uint8_t>(64, 0)));

  // Malformed key blobs.
  CHECK(!verifyEcdsaP256(blob.first(71), asBytes(message), signature));
  CHECK(!verifyEcdsaP256(std::span<const std::uint8_t>(), asBytes(message), signature));
  std::vector<std::uint8_t> longBlob = key.publicBlob();
  longBlob.push_back(0);
  CHECK(!verifyEcdsaP256(longBlob, asBytes(message), signature));

  std::vector<std::uint8_t> badMagic = key.publicBlob();
  badMagic[0] = 'X';
  CHECK(!verifyEcdsaP256(badMagic, asBytes(message), signature));
  std::vector<std::uint8_t> privateMagic = key.publicBlob();
  privateMagic[3] = '2'; // "ECS2" is the P-384 magic
  CHECK(!verifyEcdsaP256(privateMagic, asBytes(message), signature));
  std::vector<std::uint8_t> badLength = key.publicBlob();
  badLength[4] = 31; // cbKey
  CHECK(!verifyEcdsaP256(badLength, asBytes(message), signature));
  std::vector<std::uint8_t> offCurve = key.publicBlob();
  offCurve[offCurve.size() - 1] ^= 0x01; // point no longer on the curve
  CHECK(!verifyEcdsaP256(offCurve, asBytes(message), signature));
}

void testProductionKeys() {
  const auto keys = productionKeys();
  CHECK(!keys.empty());
  if (keys.empty()) {
    return;
  }
  CHECK(keys[0].id == 1);
  CHECK(keys[0].blob.size() == 72);
  if (keys[0].blob.size() == 72) {
    CHECK(std::memcmp(keys[0].blob.data(), "ECS1", 4) == 0);
    std::uint32_t keyBytes = 0;
    std::memcpy(&keyBytes, keys[0].blob.data() + 4, sizeof(keyBytes));
    CHECK(keyBytes == 32);
  }
  for (std::size_t i = 0; i < keys.size(); ++i) {
    for (std::size_t j = i + 1; j < keys.size(); ++j) {
      CHECK(keys[i].id != keys[j].id);
    }
  }

  // A signature by an ephemeral key never verifies against the production key.
  TestKey ephemeral;
  const std::string message = "not signed by the release key";
  CHECK(!verifyEcdsaP256(keys[0].blob, asBytes(message), ephemeral.sign(message)));
}

} // namespace

void runCryptoTests() {
  fsv::test::beginSuite("Crypto: SHA-256");
  testSha256Vectors();
  testSha256File();
  testHex();
  fsv::test::beginSuite("Crypto: ECDSA P-256");
  testEcdsa();
  testProductionKeys();
}
