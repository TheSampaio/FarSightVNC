// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"

#include "updater-lib/UpdateTypes.h"

#include <string_view>

using fsv::update::Channel;
using fsv::update::Version;

namespace {

Version v(std::string_view text) {
  const auto parsed = Version::parse(text);
  CHECK(parsed.has_value());
  return parsed.value_or(Version{});
}

void expectParse(std::string_view text, std::uint32_t major, std::uint32_t minor,
                 std::uint32_t fix, Channel channel) {
  const auto parsed = Version::parse(text);
  if (!parsed) {
    std::printf("  (valid input rejected: \"%.*s\")\n", static_cast<int>(text.size()), text.data());
    CHECK(parsed.has_value());
    return;
  }
  CHECK(parsed->major == major);
  CHECK(parsed->minor == minor);
  CHECK(parsed->fix == fix);
  CHECK(parsed->channel == channel);
}

void expectInvalid(std::string_view text) {
  if (Version::parse(text).has_value()) {
    std::printf("  (invalid input accepted: \"%.*s\")\n", static_cast<int>(text.size()),
                text.data());
  }
  CHECK(!Version::parse(text).has_value());
}

} // namespace

void runVersionTests() {
  fsv::test::beginSuite("Version");

  expectParse("0.2.1a", 0, 2, 1, Channel::Alpha);
  expectParse("v0.2.1a", 0, 2, 1, Channel::Alpha);
  expectParse("V1.0.0", 1, 0, 0, Channel::Stable);
  expectParse("1.2.3b", 1, 2, 3, Channel::Beta);
  expectParse("0.0.0", 0, 0, 0, Channel::Stable);
  expectParse("99999.0.0", 99999, 0, 0, Channel::Stable);
  expectParse("10.20.30", 10, 20, 30, Channel::Stable);
  expectParse("1.0.99999b", 1, 0, 99999, Channel::Beta);

  for (const char *text : {"", "v", "V", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03",
                           "1.2.3c", "1.2.3A", "1.2.3B", " 1.2.3", "1.2.3 ", "1.2.3-beta",
                           "-1.2.3", "+1.2.3", "100000.0.0", "1.100000.0", "1.0.100000", "1..3",
                           ".1.2", "1.2.", "1.2.3ab", "vv1.2.3", "1.2.3\n", "1.2.a", "a.b.c",
                           "1,2,3", "1.2.3a1", "000.0.0", "123456.1.1"}) {
    expectInvalid(text);
  }
  // Embedded NUL must not truncate the input.
  expectInvalid(std::string_view("1.2.3\0x", 7));

  // toString round trip (never emits the leading 'v').
  for (const char *text : {"0.2.1a", "1.2.3b", "0.0.0", "99999.99999.99999", "1.0.0", "12.0.7a"}) {
    CHECK(v(text).toString() == text);
    CHECK(v(std::string("v") + text).toString() == text);
  }
  CHECK(fsv::update::Version{}.toString() == "0.0.0");

  // Ordering: numeric first, then alpha < beta < stable.
  CHECK(v("0.2.1a") < v("0.2.1b"));
  CHECK(v("0.2.1b") < v("0.2.1"));
  CHECK(v("0.2.1") < v("0.2.2a"));
  CHECK(v("0.2.2a") < v("0.3.0a"));
  CHECK(v("0.2.1a") < v("0.3.0a"));
  CHECK(v("1.0.0") > v("0.99.99"));
  CHECK(v("0.10.0") > v("0.9.0")); // numeric, never lexicographic
  CHECK(v("0.0.10a") > v("0.0.9"));
  CHECK(v("2.0.0a") > v("1.99.99"));
  CHECK(!(v("0.2.1") < v("0.2.1")));

  // Equality.
  CHECK(v("0.2.1a") == v("v0.2.1a"));
  CHECK(v("1.0.0") == v("1.0.0"));
  CHECK(v("1.0.0") != v("1.0.0a"));
  CHECK(v("1.0.0a") != v("1.0.0b"));

  // Channels and prerelease flag.
  CHECK(v("1.0.0a").isPrerelease());
  CHECK(v("1.0.0b").isPrerelease());
  CHECK(!v("1.0.0").isPrerelease());
  CHECK(std::string_view(fsv::update::channelName(Channel::Alpha)) == "alpha");
  CHECK(std::string_view(fsv::update::channelName(Channel::Beta)) == "beta");
  CHECK(std::string_view(fsv::update::channelName(Channel::Stable)) == "stable");

  // Architecture of this binary.
#if defined(_M_X64)
  CHECK(std::string_view(fsv::update::buildArchitecture()) == "x64");
#elif defined(_M_IX86)
  CHECK(std::string_view(fsv::update::buildArchitecture()) == "x86");
#else
  CHECK(std::string_view(fsv::update::buildArchitecture()).empty());
#endif
}
