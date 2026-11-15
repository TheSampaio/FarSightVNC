// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Command line of fsvncupdater.exe, protocol 1 (see docs/updater/DESIGN.md,
// "Flow" step 4):
//
//   --protocol 1 --install|--recover --state "<stateDir>" --viewer "<fsvncviewer.exe>"
//   --arch x64 [--parent-pid N] [--elevated] [--relaunch]
//
// Parsing only (header-only, so updater-test covers exactly what the updater
// runs); the values are validated by the updater (paths, arch).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace fsv::update {

enum class UpdaterMode { Install, Recover };

struct UpdaterOptions {
  UpdaterMode mode = UpdaterMode::Install;
  std::wstring stateDir;
  std::wstring viewer;
  std::wstring arch;
  std::uint32_t parentPid = 0;
  bool elevated = false;
  // Start <root>\fsvncviewer.exe after a successful run ("Restart and Install
  // Now"). Only the unelevated updater relaunches, so --relaunch together with
  // --elevated is refused.
  bool relaunch = false;
};

namespace detail {

inline bool parseUpdaterPid(const std::wstring &text, std::uint32_t &pid) {
  if (text.empty() || text.size() > 10) {
    return false;
  }
  wchar_t *end = nullptr;
  const unsigned long long value = std::wcstoull(text.c_str(), &end, 10);
  if (end == nullptr || *end != L'\0' || value == 0 || value > 0xFFFFFFFFull) {
    return false;
  }
  pid = static_cast<std::uint32_t>(value);
  return true;
}

} // namespace detail

// Strict parse: any unknown argument, an option given twice (including a
// second mode flag), an option without its value or with a value that starts
// with "--", a missing required argument, or --relaunch with --elevated
// returns nullopt.
inline std::optional<UpdaterOptions> parseUpdaterArguments(const std::vector<std::wstring> &args) {
  UpdaterOptions options;
  bool protocolOk = false;
  bool haveMode = false;
  bool seenProtocol = false, seenState = false, seenViewer = false, seenArch = false;
  bool seenPid = false, seenElevated = false, seenRelaunch = false;
  // Marks a flag as seen; false if it already was.
  const auto once = [](bool &seen) {
    const bool first = !seen;
    seen = true;
    return first;
  };
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::wstring &arg = args[i];
    const bool hasValue = i + 1 < args.size() && args[i + 1].rfind(L"--", 0) != 0;
    if (arg == L"--install" || arg == L"--recover") {
      if (!once(haveMode)) {
        return std::nullopt;
      }
      options.mode = arg == L"--install" ? UpdaterMode::Install : UpdaterMode::Recover;
    } else if (arg == L"--elevated" && once(seenElevated)) {
      options.elevated = true;
    } else if (arg == L"--relaunch" && once(seenRelaunch)) {
      options.relaunch = true;
    } else if (arg == L"--protocol" && hasValue && once(seenProtocol)) {
      protocolOk = args[++i] == L"1";
    } else if (arg == L"--state" && hasValue && once(seenState)) {
      options.stateDir = args[++i];
    } else if (arg == L"--viewer" && hasValue && once(seenViewer)) {
      options.viewer = args[++i];
    } else if (arg == L"--arch" && hasValue && once(seenArch)) {
      options.arch = args[++i];
    } else if (arg == L"--parent-pid" && hasValue && once(seenPid)) {
      if (!detail::parseUpdaterPid(args[++i], options.parentPid)) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
  if (!protocolOk || !haveMode || options.stateDir.empty() || options.viewer.empty() ||
      options.arch.empty() || (options.relaunch && options.elevated)) {
    return std::nullopt;
  }
  return options;
}

} // namespace fsv::update
