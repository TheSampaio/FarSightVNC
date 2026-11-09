// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Releases.h"

#include "Json.h"
#include "Win32Util.h"

#include <cmath>

namespace fsv::update {

namespace {

constexpr double kMaxExactDouble = 9007199254740992.0; // 2^53

// A non-negative integral number that a double represents exactly.
std::optional<std::uint64_t> asAssetSize(const JsonValue *value) {
  if (value == nullptr || value->type() != JsonValue::Type::Number) {
    return std::nullopt;
  }
  const double number = value->asNumber();
  if (!(number >= 0.0) || number > kMaxExactDouble || std::floor(number) != number) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(number);
}

std::optional<ReleaseAsset> parseAsset(const JsonValue &entry) {
  if (entry.type() != JsonValue::Type::Object) {
    return std::nullopt;
  }
  const JsonValue *name = entry.find("name");
  const JsonValue *url = entry.find("browser_download_url");
  const auto size = asAssetSize(entry.find("size"));
  if (name == nullptr || name->type() != JsonValue::Type::String || name->asString().empty() ||
      url == nullptr || url->type() != JsonValue::Type::String || url->asString().empty() ||
      !size) {
    return std::nullopt;
  }
  ReleaseAsset asset;
  asset.name = name->asString();
  asset.downloadUrl = url->asString();
  asset.size = *size;
  return asset;
}

std::string stringField(const JsonValue &object, std::string_view key) {
  const JsonValue *value = object.find(key);
  return value != nullptr ? value->asString() : std::string();
}

// Returns false when the release must be skipped (missing or invalid tag).
bool parseRelease(const JsonValue &object, ReleaseInfo &release) {
  const JsonValue *tag = object.find("tag_name");
  if (tag == nullptr || tag->type() != JsonValue::Type::String) {
    return false;
  }
  const auto version = Version::parse(tag->asString());
  if (!version) {
    return false;
  }
  release.tag = tag->asString();
  release.version = *version;
  if (const JsonValue *flag = object.find("prerelease")) {
    release.githubPrerelease = flag->asBool(false);
  }
  if (const JsonValue *flag = object.find("draft")) {
    release.draft = flag->asBool(false);
  }
  release.publishedAt = stringField(object, "published_at");
  release.htmlUrl = stringField(object, "html_url");
  release.notes = stringField(object, "body"); // null -> ""
  if (const JsonValue *assets = object.find("assets")) {
    for (const JsonValue &entry : assets->items()) {
      if (auto asset = parseAsset(entry)) {
        release.assets.push_back(std::move(*asset));
      }
    }
  }
  return true;
}

} // namespace

const ReleaseAsset *ReleaseInfo::findAsset(std::string_view name) const {
  for (const ReleaseAsset &asset : assets) {
    if (asset.name == name) {
      return &asset;
    }
  }
  return nullptr;
}

std::optional<std::vector<ReleaseInfo>> parseGitHubReleases(std::string_view json) {
  return guarded(
      [&]() -> std::optional<std::vector<ReleaseInfo>> {
        const auto document = parseJson(json);
        if (!document || document->type() != JsonValue::Type::Array) {
          return std::nullopt;
        }
        std::vector<ReleaseInfo> releases;
        for (const JsonValue &element : document->items()) {
          if (element.type() != JsonValue::Type::Object) {
            return std::nullopt;
          }
          ReleaseInfo release;
          if (parseRelease(element, release)) {
            releases.push_back(std::move(release));
          }
        }
        return releases;
      },
      std::optional<std::vector<ReleaseInfo>>{});
}

bool isPrereleaseRelease(const ReleaseInfo &release) {
  return release.version.isPrerelease() || release.githubPrerelease;
}

const ReleaseInfo *selectUpdate(const std::vector<ReleaseInfo> &releases,
                                const SelectionOptions &options) {
  const ReleaseInfo *best = nullptr;
  for (const ReleaseInfo &release : releases) {
    if (release.draft || (!options.includePrereleases && isPrereleaseRelease(release)) ||
        !(release.version > options.current)) {
      continue;
    }
    if (best == nullptr || release.version > best->version) { // ties: first in input order
      best = &release;
    }
  }
  if (best != nullptr && !options.manual && options.skipped && best->version <= *options.skipped) {
    return nullptr;
  }
  return best;
}

} // namespace fsv::update
