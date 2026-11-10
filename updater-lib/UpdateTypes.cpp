// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "UpdateTypes.h"

namespace fsv::update {

namespace {

constexpr std::size_t kMaxComponentDigits = 5;

// Parses one component (1-5 decimal digits, no leading zero except "0") from
// the front of `text`, advancing it. Returns false on any violation.
bool parseComponent(std::string_view &text, std::uint32_t &value) {
  std::size_t digits = 0;
  std::uint32_t result = 0;
  while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
    if (digits >= kMaxComponentDigits) {
      return false;
    }
    result = result * 10 + static_cast<std::uint32_t>(text[digits] - '0');
    ++digits;
  }
  if (digits == 0) {
    return false;
  }
  if (digits > 1 && text[0] == '0') {
    return false;
  }
  value = result;
  text.remove_prefix(digits);
  return true;
}

bool consume(std::string_view &text, char expected) {
  if (text.empty() || text.front() != expected) {
    return false;
  }
  text.remove_prefix(1);
  return true;
}

} // namespace

const char *channelName(Channel channel) {
  switch (channel) {
  case Channel::Alpha:
    return "alpha";
  case Channel::Beta:
    return "beta";
  case Channel::Stable:
    return "stable";
  }
  return "stable";
}

std::optional<Version> Version::parse(std::string_view text) {
  if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
    text.remove_prefix(1);
  }

  Version version;
  if (!parseComponent(text, version.major) || !consume(text, '.') ||
      !parseComponent(text, version.minor) || !consume(text, '.') ||
      !parseComponent(text, version.fix)) {
    return std::nullopt;
  }

  if (!text.empty()) {
    if (text.front() == 'a') {
      version.channel = Channel::Alpha;
    } else if (text.front() == 'b') {
      version.channel = Channel::Beta;
    } else {
      return std::nullopt;
    }
    text.remove_prefix(1);
  }

  if (!text.empty()) {
    return std::nullopt;
  }
  return version;
}

std::string Version::toString() const {
  std::string text = std::to_string(major);
  text += '.';
  text += std::to_string(minor);
  text += '.';
  text += std::to_string(fix);
  if (channel == Channel::Alpha) {
    text += 'a';
  } else if (channel == Channel::Beta) {
    text += 'b';
  }
  return text;
}

const char *buildArchitecture() {
#if defined(_M_X64)
  return "x64";
#else
  return "";
#endif
}

} // namespace fsv::update
