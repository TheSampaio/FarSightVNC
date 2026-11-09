// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"

#include "updater-lib/Releases.h"

#include <string>
#include <vector>

using namespace fsv::update;

namespace {

// Modelled on the real GET /repos/{owner}/{repo}/releases response: a junk
// "name", CRLF release notes, unknown fields and a nested author object.
const char *const kFixture = R"JSON([
  {
    "url": "https://api.github.com/repos/TheSampaio/FarSightVNC/releases/1",
    "id": 1,
    "author": {"login": "TheSampaio", "id": 42, "type": "User", "site_admin": false},
    "node_id": "RE_kwDOExample",
    "tag_name": "v0.1.1a",
    "target_commitish": "main",
    "name": "# FarSightVNC v0.1.0a (alpha)",
    "draft": false,
    "prerelease": true,
    "created_at": "2026-11-01T10:00:00Z",
    "published_at": "2026-11-01T12:34:56Z",
    "assets": [
      {
        "url": "https://api.github.com/repos/TheSampaio/FarSightVNC/releases/assets/9",
        "id": 9,
        "name": "FarSightVNC-v0.1.1a.zip",
        "content_type": "application/zip",
        "state": "uploaded",
        "size": 4194304,
        "download_count": 3,
        "browser_download_url": "https://github.com/TheSampaio/FarSightVNC/releases/download/v0.1.1a/FarSightVNC-v0.1.1a.zip"
      },
      {
        "name": "farsightvnc-update.manifest",
        "size": 612,
        "browser_download_url": "https://github.com/TheSampaio/FarSightVNC/releases/download/v0.1.1a/farsightvnc-update.manifest"
      },
      {"name": "no-size.txt", "browser_download_url": "https://example.invalid/x"},
      {"name": "negative.txt", "size": -1, "browser_download_url": "https://example.invalid/x"},
      {"name": "fractional.txt", "size": 1.5, "browser_download_url": "https://example.invalid/x"},
      {"name": "huge.txt", "size": 1e300, "browser_download_url": "https://example.invalid/x"},
      "not an object",
      {"size": 5, "browser_download_url": "https://example.invalid/x"}
    ],
    "tarball_url": "https://api.github.com/repos/TheSampaio/FarSightVNC/tarball/v0.1.1a",
    "html_url": "https://github.com/TheSampaio/FarSightVNC/releases/tag/v0.1.1a",
    "body": "## Changes\r\n- first line\r\n- second line é\r\n",
    "reactions": {"total_count": 0, "+1": 0}
  },
  {
    "tag_name": "not-a-version",
    "draft": false,
    "prerelease": false,
    "assets": []
  },
  {
    "tag_name": "v0.1.0a",
    "name": "v0.1.0a",
    "draft": false,
    "prerelease": true,
    "published_at": "2026-10-01T00:00:00Z",
    "html_url": "https://github.com/TheSampaio/FarSightVNC/releases/tag/v0.1.0a",
    "body": null,
    "assets": []
  }
])JSON";

std::vector<ReleaseInfo> fixtureReleases() {
  const auto parsed = parseGitHubReleases(kFixture);
  CHECK(parsed.has_value());
  return parsed.value_or(std::vector<ReleaseInfo>{});
}

void testParseFixture() {
  const std::vector<ReleaseInfo> releases = fixtureReleases();
  CHECK(releases.size() == 2); // the invalid tag is skipped
  if (releases.size() != 2) {
    return;
  }
  const ReleaseInfo &first = releases[0];
  CHECK(first.tag == "v0.1.1a");
  CHECK(first.version.toString() == "0.1.1a");
  CHECK(first.githubPrerelease);
  CHECK(!first.draft);
  CHECK(first.publishedAt == "2026-11-01T12:34:56Z");
  CHECK(first.htmlUrl == "https://github.com/TheSampaio/FarSightVNC/releases/tag/v0.1.1a");
  CHECK(first.notes == "## Changes\r\n- first line\r\n- second line \xC3\xA9\r\n");
  // Malformed asset entries are dropped, the release is kept.
  CHECK(first.assets.size() == 2);
  const ReleaseAsset *zip = first.findAsset("FarSightVNC-v0.1.1a.zip");
  CHECK(zip != nullptr);
  if (zip != nullptr) {
    CHECK(zip->size == 4194304);
    CHECK(zip->downloadUrl ==
          "https://github.com/TheSampaio/FarSightVNC/releases/download/v0.1.1a/"
          "FarSightVNC-v0.1.1a.zip");
  }
  const ReleaseAsset *manifest = first.findAsset("farsightvnc-update.manifest");
  CHECK(manifest != nullptr && manifest->size == 612);
  CHECK(first.findAsset("farsightvnc-update.manifest.sig") == nullptr);
  CHECK(first.findAsset("FARSIGHTVNC-V0.1.1A.ZIP") == nullptr); // exact match
  CHECK(first.findAsset("no-size.txt") == nullptr);
  CHECK(first.findAsset("huge.txt") == nullptr);

  CHECK(releases[1].tag == "v0.1.0a");
  CHECK(releases[1].notes.empty()); // null body
  CHECK(releases[1].publishedAt == "2026-10-01T00:00:00Z");
}

void testParseRejects() {
  CHECK(!parseGitHubReleases("").has_value());
  CHECK(!parseGitHubReleases("not json").has_value());
  CHECK(!parseGitHubReleases("{}").has_value());
  CHECK(!parseGitHubReleases(R"({"message":"Not Found"})").has_value());
  CHECK(!parseGitHubReleases("\"x\"").has_value());
  CHECK(!parseGitHubReleases("null").has_value());
  CHECK(!parseGitHubReleases(R"([{"tag_name":"v1.0.0"}, 5])").has_value());
  CHECK(!parseGitHubReleases(R"([{"tag_name":"v1.0.0"}, []])").has_value());
  CHECK(!parseGitHubReleases(R"([{"tag_name":"v1.0.0"},])").has_value());

  const auto empty = parseGitHubReleases("[]");
  CHECK(empty.has_value() && empty->empty());

  // Invalid tags: skipped, never an error.
  const auto skipped = parseGitHubReleases(R"([
    {"tag_name":"v1.0"}, {"tag_name":"1.0.0.0"}, {"tag_name":"v1.0.0c"}, {"tag_name":" v1.0.0"},
    {"tag_name":"v01.0.0"}, {"tag_name":5}, {"tag_name":null}, {"name":"v1.0.0"},
    {"tag_name":"v1.0.0"}, {"tag_name":"2.0.0b"}
  ])");
  CHECK(skipped.has_value());
  if (skipped) {
    CHECK(skipped->size() == 2);
    if (skipped->size() == 2) {
      CHECK(skipped->at(0).tag == "v1.0.0");
      CHECK(skipped->at(1).tag == "2.0.0b");
    }
  }
}

ReleaseInfo release(const char *tag, bool githubPrerelease = false, bool draft = false) {
  ReleaseInfo info;
  info.tag = tag;
  info.version = Version::parse(tag).value_or(Version{});
  info.githubPrerelease = githubPrerelease;
  info.draft = draft;
  return info;
}

Version ver(const char *text) { return Version::parse(text).value_or(Version{}); }

SelectionOptions options(const char *current, bool includePrereleases = false) {
  SelectionOptions result;
  result.current = ver(current);
  result.includePrereleases = includePrereleases;
  return result;
}

std::string pick(const std::vector<ReleaseInfo> &releases, const SelectionOptions &opts) {
  const ReleaseInfo *chosen = selectUpdate(releases, opts);
  return chosen != nullptr ? chosen->tag : std::string();
}

void testPrereleaseClassification() {
  CHECK(!isPrereleaseRelease(release("v1.0.0")));
  CHECK(isPrereleaseRelease(release("v1.0.0a")));
  CHECK(isPrereleaseRelease(release("v1.0.0b")));
  CHECK(isPrereleaseRelease(release("v1.0.0", true))); // flagged by GitHub
  CHECK(isPrereleaseRelease(release("v1.0.0a", true)));
}

void testSelection() {
  // newer / older / identical
  CHECK(pick({release("v0.2.0")}, options("0.1.0")) == "v0.2.0");
  CHECK(pick({release("v0.1.0")}, options("0.2.0")).empty());
  CHECK(pick({release("v0.2.0")}, options("0.2.0")).empty());
  CHECK(pick({release("v0.2.1")}, options("0.2.0")) == "v0.2.1");
  CHECK(pick({release("v0.10.0")}, options("0.9.9")) == "v0.10.0"); // numeric, not lexicographic
  CHECK(pick({}, options("0.2.0")).empty());

  // alpha -> beta -> stable of the same number
  CHECK(pick({release("v1.0.0b")}, options("1.0.0a", true)) == "v1.0.0b");
  CHECK(pick({release("v1.0.0")}, options("1.0.0b", true)) == "v1.0.0");
  CHECK(pick({release("v1.0.0")}, options("1.0.0a", true)) == "v1.0.0");
  CHECK(pick({release("v1.0.0a")}, options("1.0.0b", true)).empty());
  CHECK(pick({release("v1.0.0b")}, options("1.0.0", true)).empty());
  // A stable build is never offered the prerelease of its own number.
  CHECK(pick({release("v1.0.0a")}, options("1.0.0", true)).empty());

  // Stable channel ignores suffix prereleases AND GitHub-flagged ones.
  const std::vector<ReleaseInfo> mixed = {release("v2.0.0a"), release("v1.5.0b"),
                                          release("v1.2.0", true), release("v1.1.0")};
  CHECK(pick(mixed, options("1.0.0")) == "v1.1.0");
  CHECK(pick({release("v1.0.1", true)}, options("1.0.0")).empty());
  CHECK(pick({release("v2.0.0a")}, options("1.0.0")).empty());
  // includePrereleases picks the newest of all.
  CHECK(pick(mixed, options("1.0.0", true)) == "v2.0.0a");
  CHECK(pick({release("v1.0.1", true)}, options("1.0.0", true)) == "v1.0.1");

  // Highest wins regardless of input order.
  CHECK(pick({release("v1.1.0"), release("v1.3.0"), release("v1.2.0")}, options("1.0.0")) ==
        "v1.3.0");
  CHECK(pick({release("v1.3.0"), release("v1.2.0"), release("v1.1.0")}, options("1.0.0")) ==
        "v1.3.0");

  // Equal versions: first in input order.
  ReleaseInfo firstCopy = release("v1.1.0");
  firstCopy.notes = "first";
  ReleaseInfo secondCopy = release("v1.1.0");
  secondCopy.notes = "second";
  const std::vector<ReleaseInfo> equal = {firstCopy, secondCopy};
  const ReleaseInfo *chosen = selectUpdate(equal, options("1.0.0"));
  CHECK(chosen == &equal[0]);

  // Drafts are never offered, not even the newest.
  CHECK(pick({release("v9.0.0", false, true), release("v1.1.0")}, options("1.0.0")) == "v1.1.0");
  CHECK(pick({release("v9.0.0", false, true)}, options("1.0.0", true)).empty());
  CHECK(pick({release("v9.0.0a", true, true)}, options("1.0.0", true)).empty());

  // Downgrade never.
  CHECK(pick({release("v0.9.0"), release("v0.1.0")}, options("1.0.0", true)).empty());
}

void testSkip() {
  const std::vector<ReleaseInfo> releases = {release("v1.2.0"), release("v1.1.0")};

  SelectionOptions opts = options("1.0.0");
  opts.skipped = ver("1.2.0");
  CHECK(pick(releases, opts).empty()); // exact match suppressed
  opts.skipped = ver("1.3.0");
  CHECK(pick(releases, opts).empty()); // lower than skipped suppressed
  opts.skipped = ver("1.1.0");
  CHECK(pick(releases, opts) == "v1.2.0"); // a higher release is offered again
  opts.skipped = ver("1.0.0");
  CHECK(pick(releases, opts) == "v1.2.0");

  // Manual checks ignore the skip.
  opts.manual = true;
  opts.skipped = ver("1.2.0");
  CHECK(pick(releases, opts) == "v1.2.0");
  opts.skipped = ver("9.9.9");
  CHECK(pick(releases, opts) == "v1.2.0");

  // Skipping 1.2.0b does not hide the stable 1.2.0, which is newer.
  SelectionOptions channelOpts = options("1.0.0", true);
  channelOpts.skipped = ver("1.2.0b");
  CHECK(pick({release("v1.2.0")}, channelOpts) == "v1.2.0");
  CHECK(pick({release("v1.2.0b")}, channelOpts).empty());
  CHECK(pick({release("v1.2.0a")}, channelOpts).empty());
}

void testFixtureSelection() {
  const std::vector<ReleaseInfo> releases = fixtureReleases();
  // The fixture only has alpha releases.
  CHECK(pick(releases, options("0.1.0a", true)) == "v0.1.1a");
  CHECK(pick(releases, options("0.1.0a", false)).empty());
  CHECK(pick(releases, options("0.1.1a", true)).empty());
}

} // namespace

void runReleasesTests() {
  fsv::test::beginSuite("releases");
  testParseFixture();
  testParseRejects();
  testPrereleaseClassification();
  testSelection();
  testSkip();
  testFixtureSelection();
}
