// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Manifest.h"

#include "Win32Util.h"

#include <algorithm>
#include <charconv>
#include <system_error>

namespace fsv::update {

namespace {

constexpr std::size_t kMaxPathChars = 200;
constexpr std::size_t kMaxPathSegments = 4;
constexpr std::string_view kHeader = "FarSightVNC-Update-Manifest 1";

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lowered(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), lowerAscii);
  return result;
}

bool isSegmentChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
         c == '_' || c == '-';
}

bool isDeviceName(std::string_view segment) {
  // The device name is the part before the first dot ("con.txt" is reserved).
  const std::string base = lowered(segment.substr(0, segment.find('.')));
  if (base == "con" || base == "prn" || base == "aux" || base == "nul") {
    return true;
  }
  return base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) &&
         base[3] >= '1' && base[3] <= '9';
}

bool isSafeSegment(std::string_view segment) {
  if (segment.empty() || segment.front() == '.' || segment.back() == '.') {
    return false; // also rejects "." and ".."
  }
  if (!std::all_of(segment.begin(), segment.end(), isSegmentChar)) {
    return false;
  }
  return !isDeviceName(segment);
}

// Decimal, no sign, no leading zeros (except "0"), at most maxValue.
bool parseDecimal(std::string_view text, std::uint64_t maxValue, std::uint64_t &value) {
  if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) {
    return false;
  }
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || result > maxValue) {
    return false;
  }
  value = result;
  return true;
}

// Splits line at single spaces into exactly count non-empty fields.
bool splitFields(std::string_view line, std::string_view *fields, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t space = line.find(' ');
    if (i + 1 == count) {
      if (space != std::string_view::npos || line.empty()) {
        return false;
      }
      fields[i] = line;
      return true;
    }
    if (space == std::string_view::npos || space == 0) {
      return false;
    }
    fields[i] = line.substr(0, space);
    line.remove_prefix(space + 1);
  }
  return false;
}

bool endsWithZip(std::string_view name) {
  return name.size() > 4 && lowered(name.substr(name.size() - 4)) == ".zip";
}

// Only printable ASCII, LF, and CR as part of CRLF are allowed (this also
// rejects NUL, other control characters and any non-ASCII byte).
bool hasForbiddenByte(std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\n') {
      continue;
    }
    if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        continue;
      }
      return true;
    }
    if (c < 0x20 || c >= 0x7F) {
      return true;
    }
  }
  return false;
}

// Splits into lines (LF or CRLF), allowing one optional final line
// terminator and no blank lines.
bool splitLines(std::string_view text, std::vector<std::string_view> &lines) {
  while (!text.empty()) {
    const std::size_t lf = text.find('\n');
    std::string_view line = text.substr(0, lf);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.empty()) {
      return false; // blank line
    }
    lines.push_back(line);
    if (lf == std::string_view::npos) {
      break;
    }
    text.remove_prefix(lf + 1);
  }
  return true;
}

} // namespace

bool isSafeRelativePath(std::string_view path) {
  if (path.empty() || path.size() > kMaxPathChars) {
    return false;
  }
  std::size_t segments = 0;
  for (;;) {
    const std::size_t slash = path.find('/');
    const std::string_view segment = path.substr(0, slash);
    if (++segments > kMaxPathSegments || !isSafeSegment(segment)) {
      return false;
    }
    if (slash == std::string_view::npos) {
      return true;
    }
    path.remove_prefix(slash + 1);
  }
}

std::optional<Manifest> parseManifest(std::string_view text) {
  return guarded(
      [&]() -> std::optional<Manifest> {
        if (text.size() > kMaxManifestBytes || hasForbiddenByte(text)) {
          return std::nullopt;
        }
        std::vector<std::string_view> lines;
        if (!splitLines(text, lines) || lines.size() < 4 || lines[0] != kHeader) {
          return std::nullopt;
        }

        Manifest manifest;
        std::string_view fields[4];

        // version <v> (no leading 'v')
        if (!splitFields(lines[1], fields, 2) || fields[0] != "version") {
          return std::nullopt;
        }
        const auto version = Version::parse(fields[1]);
        if (!version || fields[1].front() == 'v' || fields[1].front() == 'V') {
          return std::nullopt;
        }
        manifest.version = *version;

        // package <name> <size> <sha256>
        if (!splitFields(lines[2], fields, 4) || fields[0] != "package") {
          return std::nullopt;
        }
        std::uint64_t packageSize = 0;
        const auto packageHash = parseSha256Hex(fields[3]);
        if (!isSafeSegment(fields[1]) || !endsWithZip(fields[1]) ||
            !parseDecimal(fields[2], kMaxPackageBytes, packageSize) || !packageHash) {
          return std::nullopt;
        }
        manifest.packageName = std::string(fields[1]);
        manifest.packageSize = packageSize;
        manifest.packageSha256 = *packageHash;

        // file <path> <size> <sha256> (one or more)
        std::vector<std::string> seen;
        for (std::size_t i = 3; i < lines.size(); ++i) {
          if (manifest.files.size() >= kMaxManifestFiles) {
            return std::nullopt;
          }
          std::uint64_t size = 0;
          if (!splitFields(lines[i], fields, 4) || fields[0] != "file") {
            return std::nullopt;
          }
          const auto hash = parseSha256Hex(fields[3]);
          if (!isSafeRelativePath(fields[1]) || !parseDecimal(fields[2], kMaxFileBytes, size) ||
              !hash) {
            return std::nullopt;
          }
          std::string key = lowered(fields[1]);
          if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            return std::nullopt; // duplicate (case-insensitive)
          }
          seen.push_back(std::move(key));
          ManifestFile file;
          file.path = std::string(fields[1]);
          file.size = size;
          file.sha256 = *hash;
          manifest.files.push_back(std::move(file));
        }
        return manifest;
      },
      std::optional<Manifest>{});
}

std::optional<Manifest> verifyAndParseManifest(std::span<const std::uint8_t> manifestBytes,
                                               std::span<const std::uint8_t> signature,
                                               std::span<const TrustedKey> keys,
                                               ManifestError *error) {
  const auto fail = [error](ManifestError code) -> std::optional<Manifest> {
    if (error != nullptr) {
      *error = code;
    }
    return std::nullopt;
  };
  return guarded(
      [&]() -> std::optional<Manifest> {
        if (manifestBytes.size() > kMaxManifestBytes) {
          return fail(ManifestError::TooLarge);
        }
        if (signature.size() != kSignatureBytes) {
          return fail(ManifestError::BadSignature);
        }
        bool verified = false;
        for (const TrustedKey &key : keys) {
          if (verifyEcdsaP256(key.blob, manifestBytes, signature)) {
            verified = true;
            break;
          }
        }
        if (!verified) {
          return fail(ManifestError::BadSignature);
        }
        auto manifest = parseManifest(std::string_view(
            reinterpret_cast<const char *>(manifestBytes.data()), manifestBytes.size()));
        if (!manifest) {
          return fail(ManifestError::Malformed);
        }
        if (error != nullptr) {
          *error = ManifestError::None;
        }
        return manifest;
      },
      fail(ManifestError::Malformed));
}

} // namespace fsv::update
