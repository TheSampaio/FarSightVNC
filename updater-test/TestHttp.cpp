// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Tests of the real WinHTTP client. The default run needs no Internet access:
// every case fails (or is cancelled) before any data could be exchanged. Set
// FSV_ONLINE_TESTS=1 to also talk to the real GitHub API.

#include "TestHarness.h"
#include "TestKeys.h"

#include "updater-lib/FsUtil.h"
#include "updater-lib/Releases.h"
#include "updater-lib/UpdateService.h"

#include <windows.h>

#include <chrono>
#include <iterator>
#include <cstdio>
#include <string>
#include <thread>

using namespace fsv::update;
using fsv::test::TempDir;

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool onlineTestsEnabled() {
  wchar_t value[8] = {};
  const DWORD length = ::GetEnvironmentVariableW(L"FSV_ONLINE_TESTS", value, 8);
  return length == 1 && value[0] == L'1';
}

void testErrorNames() {
  const HttpError all[] = {HttpError::None,      HttpError::BadUrl,   HttpError::Offline,
                           HttpError::Timeout,   HttpError::Tls,      HttpError::BadStatus,
                           HttpError::RateLimited, HttpError::TooLarge, HttpError::Cancelled,
                           HttpError::Io,        HttpError::Other};
  for (std::size_t i = 0; i < std::size(all); ++i) {
    const std::string name = httpErrorName(all[i]);
    CHECK(!name.empty());
    for (std::size_t j = i + 1; j < std::size(all); ++j) {
      CHECK(name != httpErrorName(all[j]));
    }
  }
}

void testBadUrls(IHttpClient &client, const TempDir &temp) {
  const char *const urls[] = {"http://example.com/",  "ftp://example.com/", "file:///c:/x",
                              "",                      "example.com",        "https://",
                              "https:///path",         "not a url",          "https://user:pw@example.com/",
                              "HTTP://EXAMPLE.COM/"};
  const std::wstring dest = temp.child(L"never.bin");
  for (const char *url : urls) {
    HttpResponse response;
    const HttpError error = client.get(url, {}, 1024, response);
    if (error != HttpError::BadUrl) {
      std::printf("  (get '%s' -> %s)\n", url, httpErrorName(error));
    }
    CHECK(error == HttpError::BadUrl);
    CHECK(response.body.empty());
    CHECK(client.download(url, dest, 1024, {}, response) == HttpError::BadUrl);
    CHECK(!fileExists(dest));
  }
  { // CR/LF in a header would allow header injection
    HttpResponse response;
    CHECK(client.get("https://example.invalid/", {{"X-A", "b\r\nX-Evil: 1"}}, 1024, response) ==
          HttpError::BadUrl);
    CHECK(client.get("https://example.invalid/", {{"X\r\nEvil", "b"}}, 1024, response) ==
          HttpError::BadUrl);
    CHECK(client.get("https://example.invalid/", {{"", "b"}}, 1024, response) == HttpError::BadUrl);
  }
  { // a URL longer than any sane value
    HttpResponse response;
    const std::string huge = "https://example.invalid/" + std::string(10000, 'a');
    CHECK(client.get(huge, {}, 1024, response) == HttpError::BadUrl);
  }
}

void testUnresolvableHost(IHttpClient &client, const TempDir &temp) {
  HttpResponse response;
  const auto start = Clock::now();
  const HttpError error = client.get("https://nonexistent.invalid/", {}, 1024, response);
  if (error != HttpError::Offline && error != HttpError::Timeout) {
    std::printf("  (nonexistent.invalid -> %s)\n", httpErrorName(error));
  }
  CHECK(error != HttpError::None);
  CHECK(error == HttpError::Offline || error == HttpError::Timeout);
  CHECK(secondsSince(start) < 60.0);

  const std::wstring dest = temp.child(L"unresolved.bin");
  CHECK(client.download("https://nonexistent.invalid/file.zip", dest, 1024, {}, response) !=
        HttpError::None);
  CHECK(!fileExists(dest)); // no partial file left behind
}

// A client that has been cancelled stays cancelled (one client per operation).
void testCancelInFlight() {
  const std::unique_ptr<IHttpClient> client = createWinHttpClient(L"FarSightVNC-Updater/test");
  CHECK(client != nullptr);
  if (!client) {
    return;
  }
  const auto start = Clock::now();
  std::thread canceller([&] {
    ::Sleep(300);
    client->cancel();
  });
  HttpResponse response;
  const HttpError error = client->get("https://10.255.255.1/", {}, 1024, response);
  const double elapsed = secondsSince(start);
  canceller.join();
  if (error != HttpError::Cancelled && elapsed < 0.3) {
    // The network refused the connection before cancel() ran (no route).
    fsv::test::note("unroutable address failed before the cancel; in-flight cancel not exercised");
    CHECK(error != HttpError::None);
  } else {
    if (error != HttpError::Cancelled) {
      std::printf("  (cancel -> %s after %.2f s)\n", httpErrorName(error), elapsed);
    }
    CHECK(error == HttpError::Cancelled);
    CHECK(elapsed < 5.0);
  }
  // Sticky: every later request fails at once.
  const auto again = Clock::now();
  CHECK(client->get("https://10.255.255.1/", {}, 1024, response) == HttpError::Cancelled);
  CHECK(secondsSince(again) < 1.0);

  // cancel() before any request, and twice, is harmless.
  const std::unique_ptr<IHttpClient> second = createWinHttpClient(L"FarSightVNC-Updater/test");
  CHECK(second != nullptr);
  if (second) {
    second->cancel();
    second->cancel();
    CHECK(second->get("https://nonexistent.invalid/", {}, 1024, response) == HttpError::Cancelled);
  }
}

void testOnline(IHttpClient &client, const TempDir &temp) {
  char line[200];
  HttpResponse response;
  const Headers headers{{"Accept", "application/vnd.github+json"},
                        {"X-GitHub-Api-Version", "2022-11-28"}};
  const HttpError error = client.get(kReleasesApiUrl, headers, 2 * 1024 * 1024, response);
  std::snprintf(line, sizeof(line), "online: GET releases -> %s, status %u, %zu bytes",
                httpErrorName(error), response.status, response.body.size());
  fsv::test::note(line);
  if (error == HttpError::RateLimited) {
    fsv::test::note("online: GitHub rate limit reached; remaining online checks skipped");
    return;
  }
  CHECK(error == HttpError::None);
  CHECK(response.status == 200);
  const auto releases = parseGitHubReleases(response.body);
  CHECK(releases.has_value());
  if (!releases) {
    return;
  }
  std::snprintf(line, sizeof(line), "online: %zu release(s) parsed", releases->size());
  fsv::test::note(line);

  SelectionOptions options;
  options.current = *Version::parse("0.0.1a");
  options.includePrereleases = true;
  options.manual = true;
  const CheckResult check = checkForUpdate(client, options);
  std::snprintf(line, sizeof(line), "online: checkForUpdate(0.0.1a) kind=%d tag=%s detail=%s",
                static_cast<int>(check.kind), check.release ? check.release->tag.c_str() : "-",
                check.detail.c_str());
  fsv::test::note(line);
  CHECK(check.kind == CheckResult::Kind::UpdateAvailable);
  CHECK(check.release && check.release->version > options.current);

  // Download a small real asset: exercises the redirect to the CDN and streaming.
  if (check.release) {
    for (const ReleaseAsset &asset : check.release->assets) {
      if (asset.size == 0 || asset.size > 16 * 1024 * 1024) {
        continue;
      }
      const std::wstring dest = temp.child(L"asset.bin");
      std::uint64_t lastReceived = 0;
      HttpResponse downloadResponse;
      const HttpError downloadError =
          client.download(asset.downloadUrl, dest, asset.size, [&](std::uint64_t received, std::uint64_t) {
            lastReceived = received;
            return true;
          }, downloadResponse);
      std::snprintf(line, sizeof(line), "online: download %s -> %s, %llu bytes",
                    asset.name.c_str(), httpErrorName(downloadError),
                    static_cast<unsigned long long>(lastReceived));
      fsv::test::note(line);
      CHECK(downloadError == HttpError::None);
      CHECK(lastReceived == asset.size);
      CHECK(readFile(dest, asset.size).has_value());

      // A limit below the real size must abort with TooLarge and leave no file.
      const std::wstring small = temp.child(L"small.bin");
      CHECK(client.download(asset.downloadUrl, small, asset.size - 1, {}, downloadResponse) ==
            HttpError::TooLarge);
      CHECK(!fileExists(small));
      break;
    }
  }

  // A missing asset is a plain 404, not a crash.
  HttpResponse missing;
  const HttpError missingError = client.get(
      "https://github.com/TheSampaio/FarSightVNC/releases/download/v0.0.0/none.bin", {}, 1024,
      missing);
  CHECK(missingError == HttpError::BadStatus);
  CHECK(missing.status == 404);
}

} // namespace

void runHttpTests() {
  fsv::test::beginSuite("http");
  const TempDir temp;
  CHECK(!temp.path().empty());
  if (temp.path().empty()) {
    return;
  }
  testErrorNames();

  const std::unique_ptr<IHttpClient> client = createWinHttpClient(L"FarSightVNC-Updater/test");
  CHECK(client != nullptr);
  if (client) {
    testBadUrls(*client, temp);
    testUnresolvableHost(*client, temp);
  }
  testCancelInFlight();

  if (onlineTestsEnabled()) {
    const std::unique_ptr<IHttpClient> online = createWinHttpClient(L"FarSightVNC-Updater/test");
    CHECK(online != nullptr);
    if (online) {
      testOnline(*online, temp);
    }
  } else {
    fsv::test::note("online tests skipped (set FSV_ONLINE_TESTS=1 to run them)");
  }
}
