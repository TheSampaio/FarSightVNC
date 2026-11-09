// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// GitHub Releases metadata and update selection (channel filter, skip,
// downgrade prevention). Pure functions, no I/O.

#pragma once

#include "UpdateTypes.h"

namespace fsv::update {

struct ReleaseAsset {
  std::string name;
  std::string downloadUrl; // browser_download_url
  std::uint64_t size = 0;
};

struct ReleaseInfo {
  std::string tag;           // tag_name, e.g. "v0.3.0a" (authoritative, not "name")
  Version version;           // parsed from tag
  bool githubPrerelease = false;
  bool draft = false;
  std::string publishedAt;   // ISO 8601 as returned by GitHub
  std::string htmlUrl;       // release page
  std::string notes;         // body (Markdown, UTF-8, LF/CRLF as received)
  std::vector<ReleaseAsset> assets;

  const ReleaseAsset *findAsset(std::string_view name) const; // exact match
};

// Parses the JSON array of GET /repos/{owner}/{repo}/releases. Releases whose
// tag is not a valid Version are skipped. Returns nullopt if the document is
// not valid JSON or not an array of objects.
std::optional<std::vector<ReleaseInfo>> parseGitHubReleases(std::string_view json);

// A release is a prerelease if its version has a/b OR GitHub marks it so.
bool isPrereleaseRelease(const ReleaseInfo &release);

struct SelectionOptions {
  Version current;
  bool includePrereleases = false;
  std::optional<Version> skipped; // last "Skip This Version"
  bool manual = false;            // manual check ignores `skipped`
};

// Returns the highest eligible release strictly newer than `current`, or
// nullptr. Eligible: not a draft; prerelease only if includePrereleases.
// For automatic checks (manual == false) the result is suppressed when its
// version <= skipped, so notifications resume only for a newer release.
const ReleaseInfo *selectUpdate(const std::vector<ReleaseInfo> &releases,
                                const SelectionOptions &options);

} // namespace fsv::update
