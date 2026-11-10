// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a viewer based on TightVNC Viewer 2.8.90.
// Licensed under the GNU General Public License, version 2 or (at your option)
// any later version. See LICENSE.txt.
//
// Public headers of updater-lib use only standard C++ types so they compile
// regardless of the WINVER/_WIN32_WINNT the including project forces.

#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsv::update {

// Release channel, ordered so that Alpha < Beta < Stable for the same number.
enum class Channel : std::uint8_t { Alpha = 0, Beta = 1, Stable = 2 };

const char *channelName(Channel channel); // "alpha", "beta", "stable"

// vMAJOR.MINOR.FIX + optional suffix 'a' (alpha) or 'b' (beta).
struct Version {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t fix = 0;
  Channel channel = Channel::Stable;

  // Accepts an optional leading 'v'/'V', three decimal components (each 1-5
  // digits, no sign, no leading zeros except "0") and an optional lower-case
  // 'a' or 'b'. Anything else (spaces, extra components, other suffixes,
  // trailing text) is rejected.
  static std::optional<Version> parse(std::string_view text);

  std::string toString() const; // "0.2.1a" (no leading 'v')
  bool isPrerelease() const { return channel != Channel::Stable; }

  // Member order makes the defaulted comparison numeric, then by channel.
  friend auto operator<=>(const Version &, const Version &) = default;
};

using Sha256Digest = std::array<std::uint8_t, 32>;

// The architecture this binary was compiled for: "x64", or "" for anything
// else. FarSightVNC is 64-bit only: a non-x64 build has updates unavailable.
const char *buildArchitecture();

// Output names and distribution layout (see docs/updater/DESIGN.md): a flat
// folder with fsvncviewer.exe, fsvncupdater.exe, LEIAME.txt, LICENSE.txt and
// README.txt at its root.
inline constexpr wchar_t kViewerExeName[] = L"fsvncviewer.exe";
inline constexpr wchar_t kUpdaterExeName[] = L"fsvncupdater.exe";

// Official update source (see docs/updater/DESIGN.md). Never configurable at
// run time.
inline constexpr char kRepositoryOwner[] = "TheSampaio";
inline constexpr char kRepositoryName[] = "FarSightVNC";
inline constexpr char kReleasesApiUrl[] =
    "https://api.github.com/repos/TheSampaio/FarSightVNC/releases?per_page=30";
inline constexpr char kReleasesPageUrl[] =
    "https://github.com/TheSampaio/FarSightVNC/releases";
// Every asset download URL must start with this prefix.
inline constexpr char kAssetUrlPrefix[] =
    "https://github.com/TheSampaio/FarSightVNC/releases/download/";

} // namespace fsv::update
