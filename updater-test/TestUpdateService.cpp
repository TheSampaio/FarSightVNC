// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"
#include "TestKeys.h"

#include "updater-lib/Crypto.h"
#include "updater-lib/FsUtil.h"
#include "updater-lib/UpdateService.h"
#include "updater-lib/UpdaterCommandLine.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace fsv::update;
using fsv::test::asBytes;
using fsv::test::TempDir;
using fsv::test::TestKey;

namespace {

Version versionOf(const char *text) { return *Version::parse(text); }

// ------------------------------------------------------------- fake network

class FakeHttpClient final : public IHttpClient {
public:
  struct Route {
    HttpError error = HttpError::None;
    std::uint32_t status = 200;
    std::string body;
    std::uint64_t rateLimitReset = 0;
    std::optional<std::size_t> interruptAfter; // download only: stop after n bytes
    HttpError interruptError = HttpError::Offline;
  };
  struct Request {
    std::string url;
    Headers headers;
    bool download = false;
  };

  std::map<std::string, Route> routes;
  std::vector<Request> requests;
  std::atomic<bool> cancelCalled{false};

  HttpError get(const std::string &url, const Headers &headers, std::size_t maxBytes,
                HttpResponse &out) override {
    requests.push_back({url, headers, false});
    const auto it = routes.find(url);
    if (it == routes.end()) {
      out.status = 404;
      return HttpError::BadStatus;
    }
    const Route &route = it->second;
    out.status = route.status;
    out.rateLimitResetUnix = route.rateLimitReset;
    if (route.error != HttpError::None) {
      return route.error;
    }
    if (route.body.size() > maxBytes) {
      return HttpError::TooLarge;
    }
    out.body = route.body;
    out.contentLength = route.body.size();
    return HttpError::None;
  }

  HttpError download(const std::string &url, const std::wstring &destPath, std::uint64_t maxBytes,
                     const ProgressFn &progress, HttpResponse &out) override {
    requests.push_back({url, Headers{}, true});
    const auto it = routes.find(url);
    if (it == routes.end()) {
      out.status = 404;
      return HttpError::BadStatus;
    }
    const Route &route = it->second;
    out.status = route.status;
    if (route.error != HttpError::None) {
      return route.error;
    }
    if (route.body.size() > maxBytes) {
      return HttpError::TooLarge;
    }
    out.contentLength = route.body.size();
    HttpError result = HttpError::None;
    {
      std::ofstream file(destPath, std::ios::binary | std::ios::trunc);
      if (!file) {
        return HttpError::Io;
      }
      constexpr std::size_t kChunk = 256;
      std::size_t sent = 0;
      while (sent < route.body.size()) {
        if (route.interruptAfter && sent >= *route.interruptAfter) {
          result = route.interruptError;
          break;
        }
        const std::size_t n = std::min(kChunk, route.body.size() - sent);
        file.write(route.body.data() + sent, static_cast<std::streamsize>(n));
        sent += n;
        if (progress && !progress(sent, route.body.size())) {
          result = HttpError::Cancelled;
          break;
        }
      }
    }
    if (result != HttpError::None) {
      ::DeleteFileW(destPath.c_str());
    }
    return result;
  }

  void cancel() override { cancelCalled = true; }

  int countRequests(const std::string &url) const {
    int count = 0;
    for (const Request &request : requests) {
      count += request.url == url ? 1 : 0;
    }
    return count;
  }
};

// ---------------------------------------------------------------- check tests

std::string releaseJson(const std::string &tag, bool prerelease = false) {
  return R"({"tag_name":")" + tag + R"(","draft":false,"prerelease":)" +
         (prerelease ? "true" : "false") + R"(,"html_url":"https://github.com/TheSampaio/FarSightVNC/releases/tag/)" +
         tag + R"(","body":"notes","assets":[]})";
}

SelectionOptions optionsFor(const char *current, bool prereleases = true) {
  SelectionOptions options;
  options.current = versionOf(current);
  options.includePrereleases = prereleases;
  return options;
}

FakeHttpClient::Route okRoute(const std::string &body) {
  FakeHttpClient::Route route;
  route.body = body;
  return route;
}

FakeHttpClient::Route errorRoute(HttpError error, std::uint32_t status = 0) {
  FakeHttpClient::Route route;
  route.error = error;
  route.status = status;
  return route;
}

void testCheck() {
  const std::string url = kReleasesApiUrl;
  const std::string list = "[" + releaseJson("v0.3.0a", true) + "," + releaseJson("v0.2.1a", true) + "]";

  {
    FakeHttpClient http;
    http.routes[url] = okRoute(list);
    const CheckResult result = checkForUpdate(http, optionsFor("0.2.1a"));
    CHECK(result.kind == CheckResult::Kind::UpdateAvailable);
    CHECK(result.release && result.release->tag == "v0.3.0a");
    CHECK(result.release && result.release->htmlUrl.find("v0.3.0a") != std::string::npos);
    CHECK(http.requests.size() == 1);
    if (!http.requests.empty()) {
      bool accept = false;
      bool apiVersion = false;
      for (const auto &[name, value] : http.requests[0].headers) {
        accept = accept || (name == "Accept" && value == "application/vnd.github+json");
        apiVersion = apiVersion || (name == "X-GitHub-Api-Version" && value == "2022-11-28");
      }
      CHECK(accept);
      CHECK(apiVersion);
    }
  }
  {
    FakeHttpClient http;
    http.routes[url] = okRoute(list);
    const CheckResult result = checkForUpdate(http, optionsFor("0.3.0a"));
    CHECK(result.kind == CheckResult::Kind::UpToDate);
    CHECK(!result.release);
  }
  { // prerelease channel respected
    FakeHttpClient http;
    http.routes[url] = okRoute(list);
    CHECK(checkForUpdate(http, optionsFor("0.2.1a", false)).kind == CheckResult::Kind::UpToDate);
  }
  { // skipped: automatic suppresses, manual re-offers
    FakeHttpClient http;
    http.routes[url] = okRoute(list);
    SelectionOptions automatic = optionsFor("0.2.1a");
    automatic.skipped = versionOf("0.3.0a");
    CHECK(checkForUpdate(http, automatic).kind == CheckResult::Kind::UpToDate);
    SelectionOptions manual = automatic;
    manual.manual = true;
    CHECK(checkForUpdate(http, manual).kind == CheckResult::Kind::UpdateAvailable);
  }
  const struct {
    HttpError error;
    std::uint32_t status;
  } failures[] = {{HttpError::Offline, 0},   {HttpError::Timeout, 0}, {HttpError::Tls, 0},
                  {HttpError::BadStatus, 500}, {HttpError::Other, 0}, {HttpError::TooLarge, 200}};
  for (const auto &failure : failures) {
    FakeHttpClient http;
    http.routes[url] = errorRoute(failure.error, failure.status);
    const CheckResult result = checkForUpdate(http, optionsFor("0.2.1a"));
    CHECK(result.kind == CheckResult::Kind::Failed);
    CHECK(result.httpError == failure.error);
    CHECK(!result.detail.empty());
    CHECK(!result.release);
  }
  {
    FakeHttpClient http;
    http.routes[url] = errorRoute(HttpError::BadStatus, 500);
    CHECK(checkForUpdate(http, optionsFor("0.2.1a")).detail.find("500") != std::string::npos);
  }
  for (const char *body : {"", "not json", "{\"tag_name\":\"v9.9.9\"}", "42", "\"x\"", "[1,2]",
                           "[{\"tag_name\":\"v9.9.9\""}) {
    FakeHttpClient http;
    http.routes[url] = okRoute(body);
    const CheckResult result = checkForUpdate(http, optionsFor("0.2.1a"));
    CHECK(result.kind == CheckResult::Kind::Failed);
    CHECK(result.detail == "invalid response");
  }
  { // empty array is valid: up to date
    FakeHttpClient http;
    http.routes[url] = okRoute("[]");
    CHECK(checkForUpdate(http, optionsFor("0.2.1a")).kind == CheckResult::Kind::UpToDate);
  }
  {
    FakeHttpClient http;
    FakeHttpClient::Route route = errorRoute(HttpError::RateLimited, 403);
    route.rateLimitReset = 1234567;
    http.routes[url] = route;
    const CheckResult result = checkForUpdate(http, optionsFor("0.2.1a"));
    CHECK(result.kind == CheckResult::Kind::RateLimited);
    CHECK(result.retryAfterUnix == 1234567);
    CHECK(result.httpError == HttpError::RateLimited);
  }
  { // an unknown URL answers 404 in the fake -> failed, not a crash
    FakeHttpClient http;
    CHECK(checkForUpdate(http, optionsFor("0.2.1a")).kind == CheckResult::Kind::Failed);
  }
}

// ------------------------------------------------------------- check policy

CheckResult resultOf(CheckResult::Kind kind, std::uint64_t retryAfter = 0) {
  CheckResult result;
  result.kind = kind;
  result.retryAfterUnix = retryAfter;
  return result;
}

void testPolicy() {
  constexpr std::uint64_t now = 1'800'000'000;
  constexpr std::uint64_t day = kCheckIntervalSeconds;

  const UpdateSettings fresh;
  CHECK(isAutomaticCheckDue(fresh, now));

  UpdateSettings off;
  off.autoCheck = false;
  CHECK(!isAutomaticCheckDue(off, now));

  UpdateSettings waiting; // only the schedule decides; lastCheck is informational
  waiting.lastCheckUnix = now - 3600;
  waiting.nextCheckNotBeforeUnix = now + 1;
  CHECK(!isAutomaticCheckDue(waiting, now));
  waiting.nextCheckNotBeforeUnix = now;
  CHECK(isAutomaticCheckDue(waiting, now));
  waiting.nextCheckNotBeforeUnix = now + kMaxScheduleAheadSeconds;
  CHECK(!isAutomaticCheckDue(waiting, now));

  UpdateSettings future; // the clock moved back: schedule implausibly far ahead
  future.lastCheckUnix = now + 30 * day;
  future.nextCheckNotBeforeUnix = now + kMaxScheduleAheadSeconds + 1;
  CHECK(isAutomaticCheckDue(future, now));
  future.autoCheck = false;
  CHECK(!isAutomaticCheckDue(future, now));

  { // up to date: once per day
    UpdateSettings s;
    recordCheckOutcome(s, resultOf(CheckResult::Kind::UpToDate), now);
    CHECK(s.lastCheckUnix == now);
    CHECK(s.nextCheckNotBeforeUnix == now + day);
    CHECK(!isAutomaticCheckDue(s, now + 3600));
    CHECK(isAutomaticCheckDue(s, now + day));
  }
  { // update available: next request after the interval, the release is cached
    UpdateSettings s;
    s.skipped = versionOf("0.1.0a");
    CheckResult found = resultOf(CheckResult::Kind::UpdateAvailable);
    ReleaseInfo release;
    release.tag = "v0.3.0a";
    release.version = versionOf("0.3.0a");
    found.release = release;
    recordCheckOutcome(s, found, now);
    CHECK(s.lastCheckUnix == now);
    CHECK(s.nextCheckNotBeforeUnix == now + day);
    CHECK(!isAutomaticCheckDue(s, now + 60));
    CHECK(!s.pendingRelease.empty());
    CHECK(s.skipped && *s.skipped == versionOf("0.1.0a"));
    recordCheckOutcome(s, resultOf(CheckResult::Kind::Failed), now + day);
    CHECK(!s.pendingRelease.empty()); // a failure keeps the pending release
    recordCheckOutcome(s, resultOf(CheckResult::Kind::UpToDate), now + day);
    CHECK(s.pendingRelease.empty());
  }
  { // failure: retry after 6 h, last successful check untouched
    UpdateSettings s;
    s.lastCheckUnix = now - 2 * day;
    recordCheckOutcome(s, resultOf(CheckResult::Kind::Failed), now);
    CHECK(s.lastCheckUnix == now - 2 * day);
    CHECK(s.nextCheckNotBeforeUnix == now + kRetryAfterFailureSeconds);
    CHECK(!isAutomaticCheckDue(s, now + kRetryAfterFailureSeconds - 1));
    CHECK(isAutomaticCheckDue(s, now + kRetryAfterFailureSeconds));
  }
  { // rate limit: honour the reset, but never less than the minimum backoff
    const struct {
      std::uint64_t reset;
      std::uint64_t expected;
    } cases[] = {{0, now + kMinRateLimitBackoffSeconds},
                 {now - 5, now + kMinRateLimitBackoffSeconds},
                 {now + 100, now + kMinRateLimitBackoffSeconds},
                 {now + 2 * kMinRateLimitBackoffSeconds, now + 2 * kMinRateLimitBackoffSeconds}};
    for (const auto &item : cases) {
      UpdateSettings s;
      recordCheckOutcome(s, resultOf(CheckResult::Kind::RateLimited, item.reset), now);
      CHECK(s.nextCheckNotBeforeUnix == item.expected);
    }
  }

  const Version alpha = versionOf("0.2.1a");
  const Version stable = versionOf("1.0.0");
  UpdateSettings channel;
  CHECK(effectiveIncludePrereleases(channel, alpha));
  CHECK(!effectiveIncludePrereleases(channel, stable));
  channel.includePrereleases = false;
  CHECK(!effectiveIncludePrereleases(channel, alpha));
  channel.includePrereleases = true;
  CHECK(effectiveIncludePrereleases(channel, stable));

  CHECK(std::string(stageErrorName(StageError::None)) == "none");
  CHECK(std::string(stageErrorName(StageError::BadSignature)) == "bad-signature");
  for (int i = 0; i <= static_cast<int>(StageError::Io); ++i) {
    const std::string name = stageErrorName(static_cast<StageError>(i));
    CHECK(!name.empty() && name != "unknown");
  }
  for (int i = 0; i <= static_cast<int>(HttpError::Other); ++i) {
    const std::string name = httpErrorName(static_cast<HttpError>(i));
    CHECK(!name.empty() && name != "unknown");
  }
}

// ------------------------------------------------------ guarded check run

class MemoryStore final : public IUpdateSettingsStore {
public:
  mutable UpdateSettings stored;
  mutable int saves = 0;
  UpdateSettings load() const override { return stored; }
  bool save(const UpdateSettings &settings) const override {
    stored = settings;
    ++saves;
    return true;
  }
};

class FakeLock final : public ICheckLock {
public:
  bool heldElsewhere = false;
  int acquired = 0;
  int released = 0;
  bool tryAcquire() override {
    if (heldElsewhere) {
      return false;
    }
    ++acquired;
    return true;
  }
  void release() override { ++released; }
  unsigned long lastError() const override { return heldElsewhere ? error : 0; }
  unsigned long error = 0; // with heldElsewhere: simulate a mutex failure
};

struct CheckBench {
  MemoryStore store;
  FakeLock lock;
  FakeHttpClient http;
  std::uint64_t now = 1'800'000'000;
  Version current = versionOf("0.2.1a");

  CheckRun run(CheckTrigger trigger) {
    return runCheck(trigger, http, store, lock, current, [this] { return now; });
  }
  int requests() const { return static_cast<int>(http.requests.size()); }
};

void testGuardedCheck() {
  const std::string url = kReleasesApiUrl;
  const std::string newer = "[" + releaseJson("v0.3.0a", true) + "]";
  const std::string same = "[" + releaseJson("v0.2.1a", true) + "]";
  constexpr std::uint64_t day = kCheckIntervalSeconds;

  { // up to date: next automatic check exactly 24 h later
    CheckBench b;
    b.http.routes[url] = okRoute(same);
    const std::uint64_t start = b.now;
    CheckRun run = b.run(CheckTrigger::Automatic);
    CHECK(run.status == CheckRun::Status::Checked);
    CHECK(run.result.kind == CheckResult::Kind::UpToDate);
    CHECK(b.store.stored.lastCheckUnix == start);
    CHECK(b.store.stored.nextCheckNotBeforeUnix == start + day);
    CHECK(b.lock.acquired == 1 && b.lock.released == 1);
    b.now = start + day - 1;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    CHECK(b.requests() == 1);
    CHECK(b.lock.acquired == 2 && b.lock.released == 2); // released when not due too
    b.now = start + day;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::Checked);
    CHECK(b.requests() == 2);
    CHECK(b.store.stored.nextCheckNotBeforeUnix == start + 2 * day);
  }
  { // failure: retried after 6 h, not before
    CheckBench b;
    b.http.routes[url] = errorRoute(HttpError::Offline);
    const std::uint64_t start = b.now;
    CheckRun run = b.run(CheckTrigger::Automatic);
    CHECK(run.status == CheckRun::Status::Checked);
    CHECK(run.result.kind == CheckResult::Kind::Failed);
    CHECK(b.store.stored.lastCheckUnix == 0);
    CHECK(b.store.stored.nextCheckNotBeforeUnix == start + kRetryAfterFailureSeconds);
    b.now = start + kRetryAfterFailureSeconds - 1;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    b.now = start + kRetryAfterFailureSeconds;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::Checked);
    CHECK(b.requests() == 2);
  }
  { // rate limit: the reset time is respected
    CheckBench b;
    FakeHttpClient::Route route = errorRoute(HttpError::RateLimited, 403);
    route.rateLimitReset = b.now + 2 * 3600;
    b.http.routes[url] = route;
    const std::uint64_t start = b.now;
    CHECK(b.run(CheckTrigger::Automatic).result.kind == CheckResult::Kind::RateLimited);
    CHECK(b.store.stored.nextCheckNotBeforeUnix == start + 2 * 3600);
    b.now = start + 2 * 3600 - 1;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    b.now = start + 2 * 3600;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::Checked);
  }
  { // found: offered again on the next launch from the cache, one request per interval
    CheckBench b;
    b.http.routes[url] = okRoute(newer);
    const std::uint64_t start = b.now;
    CheckRun run = b.run(CheckTrigger::Automatic);
    CHECK(run.result.kind == CheckResult::Kind::UpdateAvailable);
    CHECK(run.result.release && run.result.release->tag == "v0.3.0a");
    CHECK(b.store.stored.nextCheckNotBeforeUnix == start + day);
    b.now += 60; // another process, or the next launch
    CheckRun again = b.run(CheckTrigger::Automatic);
    CHECK(again.status == CheckRun::Status::Cached);
    CHECK(again.result.kind == CheckResult::Kind::UpdateAvailable);
    CHECK(again.result.release && again.result.release->tag == "v0.3.0a");
    CHECK(again.result.release && again.result.release->htmlUrl.find("v0.3.0a") != std::string::npos);
    CHECK(b.requests() == 1);
    // Skipped meanwhile: not offered and dropped from the cache.
    b.store.stored.skipped = versionOf("0.3.0a");
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    CHECK(b.store.stored.pendingRelease.empty());
    b.now = start + day;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::Checked);
    CHECK(b.requests() == 2);
  }
  { // the cached release is not offered once it is installed (running version >= it)
    CheckBench b;
    b.http.routes[url] = okRoute(newer);
    b.run(CheckTrigger::Automatic);
    b.current = versionOf("0.3.0a");
    b.now += 60;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    CHECK(b.store.stored.pendingRelease.empty());
  }
  { // round trip of the cache keeps notes with quotes, backslashes and control characters
    ReleaseInfo release;
    release.tag = "v0.3.0a";
    release.version = versionOf("0.3.0a");
    release.githubPrerelease = true;
    release.publishedAt = "2026-11-14T10:00:00Z";
    release.htmlUrl = "https://github.com/TheSampaio/FarSightVNC/releases/tag/v0.3.0a";
    release.notes = "line \"one\"\r\n\tback\\slash \xc3\xa9";
    release.assets.push_back({"farsightvnc-update.manifest", "https://example.invalid/m", 123});
    const std::optional<std::vector<ReleaseInfo>> parsed =
        parseGitHubReleases(serializeReleaseForCache(release));
    CHECK(parsed && parsed->size() == 1);
    if (parsed && parsed->size() == 1) {
      const ReleaseInfo &back = parsed->front();
      CHECK(back.tag == release.tag && back.notes == release.notes);
      CHECK(back.githubPrerelease && back.publishedAt == release.publishedAt);
      CHECK(back.assets.size() == 1 && back.assets[0].size == 123 &&
            back.assets[0].downloadUrl == "https://example.invalid/m");
    }
    release.notes.assign(kMaxCachedNotesBytes + 10, 'x');
    CHECK(serializeReleaseForCache(release).size() < kMaxPendingReleaseBytes);
  }
  { // manual ignores the interval and the skipped version; automatic honours both
    CheckBench b;
    b.http.routes[url] = okRoute(newer);
    b.store.stored.nextCheckNotBeforeUnix = b.now + day;
    b.store.stored.skipped = versionOf("0.3.0a");
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    CHECK(b.requests() == 0);
    CheckRun manual = b.run(CheckTrigger::Manual);
    CHECK(manual.status == CheckRun::Status::Checked);
    CHECK(manual.result.kind == CheckResult::Kind::UpdateAvailable);
    CHECK(b.requests() == 1);
    CHECK(b.lock.acquired == 1); // only the automatic run took the lock
    b.store.stored.nextCheckNotBeforeUnix = 0;
    CHECK(b.run(CheckTrigger::Automatic).result.kind == CheckResult::Kind::UpToDate);
  }
  { // manual check while automatic checks are off
    CheckBench b;
    b.http.routes[url] = okRoute(same);
    b.store.stored.autoCheck = false;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    CHECK(b.run(CheckTrigger::Manual).status == CheckRun::Status::Checked);
    CHECK(!b.store.stored.autoCheck);
  }
  { // another process holds the lock: nothing is requested or saved
    CheckBench b;
    b.http.routes[url] = okRoute(same);
    b.lock.heldElsewhere = true;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::Busy);
    CHECK(b.requests() == 0);
    CHECK(b.store.saves == 0);
    CHECK(b.lock.released == 0);
    b.lock.error = 87; // the mutex could not be created: reported as such
    CheckRun failed = b.run(CheckTrigger::Automatic);
    CHECK(failed.status == CheckRun::Status::LockFailed);
    CHECK(failed.result.detail.find("87") != std::string::npos);
    CHECK(b.requests() == 0);
    b.lock.error = 0;
    // ...and when it checked meanwhile, this process finds it not due.
    b.lock.heldElsewhere = false;
    b.store.stored.nextCheckNotBeforeUnix = b.now + day;
    CHECK(b.run(CheckTrigger::Automatic).status == CheckRun::Status::NotDue);
    CHECK(b.requests() == 0);
  }
  { // a cancelled request (viewer exiting) records nothing
    CheckBench b;
    b.http.routes[url] = errorRoute(HttpError::Cancelled);
    CheckRun run = b.run(CheckTrigger::Automatic);
    CHECK(run.status == CheckRun::Status::Checked);
    CHECK(run.result.httpError == HttpError::Cancelled);
    CHECK(b.store.saves == 0);
    CHECK(b.lock.released == 1);
  }
  { // the real named mutex: a second owner (another thread here) is refused
    wchar_t name[96];
    swprintf_s(name, L"Local\\FarSightVNC-UpdateCheck-test-%lu-%llu", ::GetCurrentProcessId(),
               static_cast<unsigned long long>(::GetTickCount64()));
    NamedMutexCheckLock first(name);
    CHECK(first.tryAcquire());
    bool otherGot = true;
    std::thread([&] {
      NamedMutexCheckLock second(name);
      otherGot = second.tryAcquire();
    }).join();
    CHECK(!otherGot);
    first.release();
    bool otherAfter = false;
    std::thread([&] {
      NamedMutexCheckLock second(name);
      otherAfter = second.tryAcquire();
      second.release();
    }).join();
    CHECK(otherAfter);
  }
}

// --------------------------------------------- updater command line, results

std::vector<std::wstring> baseUpdaterArgs() {
  return {L"--protocol", L"1",    L"--install", L"--state", L"C:\\s", L"--viewer",
          L"C:\\v\\fsvncviewer.exe", L"--arch", L"x64",     L"--parent-pid", L"42"};
}

std::vector<std::wstring> plus(std::vector<std::wstring> args, const wchar_t *extra) {
  args.push_back(extra);
  return args;
}

void testUpdaterCommandLine() {
  const std::optional<UpdaterOptions> base = parseUpdaterArguments(baseUpdaterArgs());
  CHECK(base.has_value());
  CHECK(base && base->mode == UpdaterMode::Install && !base->relaunch && !base->elevated);
  CHECK(base && base->parentPid == 42u && base->stateDir == L"C:\\s" && base->arch == L"x64");

  const std::optional<UpdaterOptions> relaunch =
      parseUpdaterArguments(plus(baseUpdaterArgs(), L"--relaunch"));
  CHECK(relaunch && relaunch->relaunch && !relaunch->elevated);
  const std::optional<UpdaterOptions> elevated =
      parseUpdaterArguments(plus(baseUpdaterArgs(), L"--elevated"));
  CHECK(elevated && elevated->elevated && !elevated->relaunch);
  // The elevated child never relaunches the viewer.
  CHECK(!parseUpdaterArguments(plus(plus(baseUpdaterArgs(), L"--relaunch"), L"--elevated")));

  std::vector<std::wstring> recover = baseUpdaterArgs();
  recover[2] = L"--recover";
  const std::optional<UpdaterOptions> recovered = parseUpdaterArguments(plus(recover, L"--relaunch"));
  CHECK(recovered && recovered->mode == UpdaterMode::Recover && recovered->relaunch);

  CHECK(!parseUpdaterArguments(plus(baseUpdaterArgs(), L"--relaunch=1")));
  // duplicates, a second mode, and values that look like options
  CHECK(!parseUpdaterArguments(plus(plus(baseUpdaterArgs(), L"--relaunch"), L"--relaunch")));
  CHECK(!parseUpdaterArguments(plus(baseUpdaterArgs(), L"--recover")));
  CHECK(!parseUpdaterArguments(plus(baseUpdaterArgs(), L"--install")));
  CHECK(!parseUpdaterArguments(plus(plus(baseUpdaterArgs(), L"--state"), L"C:\\other")));
  CHECK(!parseUpdaterArguments(plus(plus(baseUpdaterArgs(), L"--parent-pid"), L"7")));
  std::vector<std::wstring> dashValue = baseUpdaterArgs();
  dashValue[4] = L"--relaunch"; // --state --relaunch
  CHECK(!parseUpdaterArguments(dashValue));
  CHECK(!parseUpdaterArguments(plus(baseUpdaterArgs(), L"--unknown")));
  CHECK(!parseUpdaterArguments(plus(baseUpdaterArgs(), L"--state"))); // option without value
  std::vector<std::wstring> wrongProtocol = baseUpdaterArgs();
  wrongProtocol[1] = L"2";
  CHECK(!parseUpdaterArguments(wrongProtocol));
  std::vector<std::wstring> noMode = baseUpdaterArgs();
  noMode.erase(noMode.begin() + 2);
  CHECK(!parseUpdaterArguments(noMode));
  for (const wchar_t *pid : {L"0", L"-1", L"12x", L"", L"4294967296", L"12345678901"}) {
    std::vector<std::wstring> bad = baseUpdaterArgs();
    bad[10] = pid;
    CHECK(!parseUpdaterArguments(bad));
  }
  std::vector<std::wstring> maxPid = baseUpdaterArgs();
  maxPid[10] = L"4294967295";
  CHECK(parseUpdaterArguments(maxPid) && parseUpdaterArguments(maxPid)->parentPid == 4294967295u);
  std::vector<std::wstring> noPid = baseUpdaterArgs();
  noPid.resize(9);
  CHECK(parseUpdaterArguments(noPid) && parseUpdaterArguments(noPid)->parentPid == 0u);
  CHECK(!parseUpdaterArguments({}));
}

void testInstallOutcome(const TempDir &temp) {
  const std::optional<InstallOutcome> ok = parseInstallOutcome("ok 1.3.0a");
  CHECK(ok && ok->kind == InstallOutcome::Kind::Installed && ok->version == versionOf("1.3.0a"));
  CHECK(parseInstallOutcome("  ok 1.3.0a\r\n").has_value());
  const std::optional<InstallOutcome> failed = parseInstallOutcome("failed rolled-back");
  CHECK(failed && failed->kind == InstallOutcome::Kind::Failed && failed->detail == "rolled-back");
  const std::optional<InstallOutcome> declined = parseInstallOutcome("declined 1.3.0a");
  CHECK(declined && declined->kind == InstallOutcome::Kind::Declined && declined->version);
  const std::optional<InstallOutcome> unknown = parseInstallOutcome("declined unknown");
  CHECK(unknown && unknown->kind == InstallOutcome::Kind::Declined && !unknown->version);
  for (const char *bad : {"", "ok", "ok ", "ok banana", "ok 1.3.0a extra", "done 1.3.0a",
                          "OK 1.3.0a", "declined nope"}) {
    CHECK(!parseInstallOutcome(bad));
  }

  const std::wstring state = joinPath(temp.path(), L"result-state");
  CHECK(ensureDirectory(state));
  const std::wstring resultPath = joinPath(state, kLastResultFileName);
  const std::wstring ackPath = joinPath(state, kLastResultAckFileName);
  const Version running = versionOf("1.3.0a");

  CHECK(!acknowledgeInstalledUpdate(state, running)); // no file
  CHECK(writeFileAtomic(resultPath, "ok 1.2.0a"));    // an older install
  CHECK(!acknowledgeInstalledUpdate(state, running));
  CHECK(fileExists(resultPath));
  CHECK(writeFileAtomic(resultPath, "failed rolled-back"));
  CHECK(!acknowledgeInstalledUpdate(state, running));
  CHECK(fileExists(resultPath) && !fileExists(ackPath));

  CHECK(writeFileAtomic(resultPath, "ok 1.3.0a"));
  CHECK(acknowledgeInstalledUpdate(state, running)); // told once...
  CHECK(!fileExists(resultPath) && fileExists(ackPath));
  CHECK(!acknowledgeInstalledUpdate(state, running)); // ...never again
  CHECK(writeFileAtomic(resultPath, "ok 1.3.0a"));    // a later install replaces the ack
  CHECK(acknowledgeInstalledUpdate(state, running));
  CHECK(!fileExists(resultPath) && fileExists(ackPath));
  CHECK(!acknowledgeInstalledUpdate(std::wstring(), running));
}

// ----------------------------------------------------------------- settings

std::wstring uniqueSubKey() {
  static std::atomic<unsigned> counter{0};
  LARGE_INTEGER ticks{};
  ::QueryPerformanceCounter(&ticks);
  wchar_t name[160] = {};
  std::swprintf(name, sizeof(name) / sizeof(name[0]),
                L"Software\\FarSightVNC\\Test\\updater-%lu-%u-%llx",
                static_cast<unsigned long>(::GetCurrentProcessId()), ++counter,
                static_cast<unsigned long long>(ticks.QuadPart));
  return name;
}

void setRaw(const std::wstring &subKey, const wchar_t *name, DWORD type, const void *data,
            DWORD size) {
  HKEY key = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) == ERROR_SUCCESS) {
    ::RegSetValueExW(key, name, 0, type, static_cast<const BYTE *>(data), size);
    ::RegCloseKey(key);
  }
}

void setString(const std::wstring &subKey, const wchar_t *name, const std::wstring &value) {
  setRaw(subKey, name, REG_SZ, value.c_str(),
         static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void testSettings() {
  const std::wstring subKey = uniqueSubKey();
  const UpdateSettingsStore store(subKey);

  { // missing key: defaults
    const UpdateSettings loaded = store.load();
    CHECK(loaded.autoCheck);
    CHECK(!loaded.includePrereleases);
    CHECK(!loaded.skipped);
    CHECK(loaded.lastCheckUnix == 0);
    CHECK(loaded.nextCheckNotBeforeUnix == 0);
  }
  {
    UpdateSettings settings;
    settings.autoCheck = false;
    settings.includePrereleases = true;
    settings.skipped = versionOf("0.3.0b");
    settings.lastCheckUnix = 0x1'2345'6789ull; // needs more than 32 bits
    settings.nextCheckNotBeforeUnix = 0x2'0000'0001ull;
    CHECK(store.save(settings));
    const UpdateSettings loaded = store.load();
    CHECK(!loaded.autoCheck);
    CHECK(loaded.includePrereleases && *loaded.includePrereleases);
    CHECK(loaded.skipped && *loaded.skipped == versionOf("0.3.0b"));
    CHECK(loaded.lastCheckUnix == settings.lastCheckUnix);
    CHECK(loaded.nextCheckNotBeforeUnix == settings.nextCheckNotBeforeUnix);
  }
  { // the pending release round-trips as bytes; empty deletes it
    UpdateSettings settings;
    settings.pendingRelease = std::string("[{\"tag_name\":\"v9.9.9\"}]\x01\xc3\xa9", 26);
    CHECK(store.save(settings));
    CHECK(store.load().pendingRelease == settings.pendingRelease);
    settings.pendingRelease.clear();
    CHECK(store.save(settings));
    CHECK(store.load().pendingRelease.empty());
  }
  { // explicit false is distinct from unset
    UpdateSettings settings;
    settings.includePrereleases = false;
    CHECK(store.save(settings));
    const UpdateSettings loaded = store.load();
    CHECK(loaded.includePrereleases && !*loaded.includePrereleases);
    CHECK(loaded.autoCheck);
    CHECK(!loaded.skipped);
  }
  { // unset optionals delete the stored values
    UpdateSettings settings;
    CHECK(store.save(settings));
    const UpdateSettings loaded = store.load();
    CHECK(!loaded.includePrereleases);
    CHECK(!loaded.skipped);
    HKEY key = nullptr;
    CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_QUERY_VALUE, &key) ==
          ERROR_SUCCESS);
    CHECK(::RegQueryValueExW(key, L"IncludePrereleases", nullptr, nullptr, nullptr, nullptr) ==
          ERROR_FILE_NOT_FOUND);
    CHECK(::RegQueryValueExW(key, L"SkippedVersion", nullptr, nullptr, nullptr, nullptr) ==
          ERROR_FILE_NOT_FOUND);
    ::RegCloseKey(key);
  }
  { // invalid stored values fall back to defaults
    const wchar_t *const badVersions[] = {L"garbage", L"", L"1.2", L"v1.2.3c", L"1.2.3 "};
    for (const wchar_t *text : badVersions) {
      setString(subKey, L"SkippedVersion", text);
      CHECK(!store.load().skipped);
    }
    setString(subKey, L"SkippedVersion", std::wstring(200, L'1'));
    CHECK(!store.load().skipped);
    const DWORD one = 1;
    setRaw(subKey, L"SkippedVersion", REG_DWORD, &one, sizeof(one));
    CHECK(!store.load().skipped);

    setString(subKey, L"AutoCheck", L"0");
    CHECK(store.load().autoCheck); // wrong type -> default (true)
    setString(subKey, L"IncludePrereleases", L"1");
    CHECK(!store.load().includePrereleases);
    const std::uint64_t big = 1;
    setRaw(subKey, L"AutoCheck", REG_QWORD, &big, sizeof(big));
    CHECK(store.load().autoCheck);
    setRaw(subKey, L"LastCheck", REG_DWORD, &one, sizeof(one));
    CHECK(store.load().lastCheckUnix == 0);
    setString(subKey, L"NextCheckNotBefore", L"5");
    CHECK(store.load().nextCheckNotBeforeUnix == 0);
    const char binary[3] = {1, 2, 3};
    setRaw(subKey, L"LastCheck", REG_QWORD, binary, sizeof(binary)); // wrong size
    CHECK(store.load().lastCheckUnix == 0);
  }
  { // an empty sub-key must never write into the HKCU root
    const UpdateSettingsStore broken{std::wstring()};
    CHECK(!broken.save(UpdateSettings{}));
    CHECK(broken.load().autoCheck);
  }

  CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subKey.c_str()) == ERROR_SUCCESS);
  HKEY gone = nullptr;
  CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_QUERY_VALUE, &gone) !=
        ERROR_SUCCESS);
  // Removes the shared test parent only when no other test key is left in it.
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\FarSightVNC\\Test");
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\FarSightVNC"); // only if it is now empty

  // The administrator policy cannot be set without elevation; just exercise it.
  const bool disabled = updatesDisabledByPolicy();
  fsv::test::note(disabled ? "policy DisableUpdateCheck is set on this machine"
                : "policy DisableUpdateCheck is not set (cannot be tested without admin)");
}

// ------------------------------------------------------------------ staging

using FileList = std::vector<std::pair<std::string, std::string>>;

// Incompressible, label-dependent bytes so the zip is several fake-network chunks long.
std::string contentOf(const char *label, std::size_t size) {
  std::uint32_t state = 2166136261u;
  for (const char *c = label; *c != '\0'; ++c) {
    state = (state ^ static_cast<unsigned char>(*c)) * 16777619u;
  }
  std::string out(size, '\0');
  for (char &c : out) {
    state = state * 1664525u + 1013904223u;
    c = static_cast<char>(state >> 24);
  }
  return out;
}

std::wstring widen(const std::string &text) { return utf8ToWide(text); }

class StageFixture {
public:
  StageFixture() {
    files = {{"fsvncviewer.exe", contentOf("viewer-exe", 5000)},
             {"fsvncupdater.exe", contentOf("updater-exe", 3000)},
             {"LICENSE.txt", "license text\n"}};
    keys.push_back(TrustedKey{1, key.publicBlob()});
  }

  // (Re)computes zip, manifest, signature, release metadata and fake routes
  // from the public members; tests tweak them (or the routes) beforehand.
  void publish() {
    const std::string tag = "v" + tagVersion;
    const std::string packageName = "FarSightVNC-" + tag + ".zip";
    zip = fsv::test::buildSimpleZip(zipFiles ? *zipFiles : files);
    manifestText = "FarSightVNC-Update-Manifest 1\nversion " + manifestVersion + "\npackage " +
                   packageName + " " + std::to_string(zip.size()) + " " +
                   toHex(sha256(asBytes(zip))) + "\n";
    for (const auto &[path, content] : files) {
      manifestText += "file " + path + " " + std::to_string(content.size()) + " " +
                      toHex(sha256(asBytes(content))) + "\n";
    }
    const std::vector<std::uint8_t> raw = key.sign(manifestText);
    signature.assign(raw.begin(), raw.end());

    release = ReleaseInfo{};
    release.tag = tag;
    release.version = versionOf(tagVersion.c_str());
    release.githubPrerelease = true;
    const std::string base = baseUrl.empty() ? std::string(kAssetUrlPrefix) + tag + "/" : baseUrl;
    if (!omitManifest) {
      release.assets.push_back({kManifestAssetName, base + kManifestAssetName, manifestText.size()});
    }
    if (!omitSignature) {
      release.assets.push_back({kSignatureAssetName, base + kSignatureAssetName, signature.size()});
    }
    if (!omitPackage) {
      release.assets.push_back({packageName, base + packageName, zip.size()});
    }
    http.routes.clear();
    http.routes[base + kManifestAssetName] = okRoute(manifestText);
    http.routes[base + kSignatureAssetName] = okRoute(signature);
    http.routes[base + packageName] = okRoute(zip);
    manifestUrl = base + kManifestAssetName;
    signatureUrl = base + kSignatureAssetName;
    packageUrl = base + packageName;
  }

  StageResult stage(const char *current = "0.2.1a", const ProgressFn &progress = {}) {
    return stageUpdate(http, release, versionOf(current), keys, state, progress);
  }

  std::wstring pending() const { return joinPath(state, kPendingDirName); }

  TestKey key;
  std::vector<TrustedKey> keys;
  FileList files;
  std::optional<FileList> zipFiles; // zip contents when they must differ from `files`
  std::string tagVersion = "0.3.0a";
  std::string manifestVersion = "0.3.0a";
  std::string baseUrl; // overrides the official download prefix
  bool omitManifest = false;
  bool omitSignature = false;
  bool omitPackage = false;
  std::wstring state;

  std::string zip;
  std::string manifestText;
  std::string signature;
  ReleaseInfo release;
  FakeHttpClient http;
  std::string manifestUrl;
  std::string signatureUrl;
  std::string packageUrl;
};

bool pendingGone(const StageFixture &fx) { return !directoryExists(fx.pending()); }

std::atomic<unsigned> g_stateCounter{0};

std::wstring newState(const TempDir &temp) {
  return temp.child(L"state" + std::to_wstring(++g_stateCounter));
}

void expectFailure(StageFixture &fx, StageError expected, const char *what,
                   const char *current = "0.2.1a") {
  const StageResult result = fx.stage(current);
  if (result.error != expected) {
    std::printf("  (%s: got %s '%s')\n", what, stageErrorName(result.error), result.detail.c_str());
  }
  CHECK(result.error == expected);
  CHECK(!result.manifest);
  CHECK(!result.detail.empty());
  CHECK(pendingGone(fx));
  CHECK(!loadStagedUpdate(fx.state, fx.keys));
}

void testStageSuccess(const TempDir &temp) {
  StageFixture fx;
  fx.state = newState(temp);
  fx.publish();

  std::vector<std::pair<std::uint64_t, std::uint64_t>> ticks;
  bool readyDuringDownload = false;
  const StageResult result = fx.stage("0.2.1a", [&](std::uint64_t received, std::uint64_t total) {
    ticks.emplace_back(received, total);
    readyDuringDownload = readyDuringDownload || fileExists(joinPath(fx.pending(), kReadyMarkerName));
    return true;
  });
  CHECK(result.error == StageError::None);
  CHECK(result.manifest && result.manifest->version == versionOf("0.3.0a"));
  CHECK(result.manifest && result.manifest->files.size() == 3);
  CHECK(result.manifest && result.manifest->packageSize == fx.zip.size());
  CHECK(!ticks.empty());
  CHECK(!ticks.empty() && ticks.back().first == fx.zip.size());
  CHECK(!ticks.empty() && ticks.back().second == fx.zip.size());
  CHECK(!readyDuringDownload);

  // Layout of the staged update.
  const std::wstring pending = fx.pending();
  CHECK(fileExists(joinPath(pending, kReadyMarkerName)));
  CHECK(readFile(joinPath(pending, kReadyMarkerName), 64) == std::optional<std::string>("0.3.0a"));
  CHECK(!fileExists(joinPath(pending, L"package.zip")));
  CHECK(!fileExists(joinPath(pending, L"package.zip.part")));
  CHECK(readFile(joinPath(pending, L"farsightvnc-update.manifest"), 100000) ==
        std::optional<std::string>(fx.manifestText));
  CHECK(readFile(joinPath(pending, L"farsightvnc-update.manifest.sig"), 100) ==
        std::optional<std::string>(fx.signature));
  for (const auto &[path, content] : fx.files) {
    std::wstring relative = widen(path);
    for (wchar_t &c : relative) {
      c = c == L'/' ? L'\\' : c;
    }
    CHECK(readFile(joinPath(joinPath(pending, L"files"), relative), 100000) ==
          std::optional<std::string>(content));
  }

  // Requests: manifest, signature, package, in that order, each once.
  CHECK(fx.http.requests.size() == 3);
  CHECK(fx.http.countRequests(fx.manifestUrl) == 1);
  CHECK(fx.http.countRequests(fx.signatureUrl) == 1);
  CHECK(fx.http.countRequests(fx.packageUrl) == 1);
  CHECK(fx.http.requests.size() == 3 && fx.http.requests[2].download);

  const std::optional<Manifest> loaded = loadStagedUpdate(fx.state, fx.keys);
  CHECK(loaded && loaded->version == versionOf("0.3.0a"));
  CHECK(loaded && loaded->files.size() == 3);

  // The production keys do not trust a test-signed update.
  CHECK(!loadStagedUpdate(fx.state, productionKeys()));

  // A second staging run replaces the previous pending directory.
  fx.http.requests.clear();
  CHECK(fx.stage().error == StageError::None);
  CHECK(loadStagedUpdate(fx.state, fx.keys).has_value());

  // Tampering with a staged update is detected on load.
  CHECK(writeFileAtomic(joinPath(joinPath(pending, L"files"), L"LICENSE.txt"), "license text!\n"));
  CHECK(!loadStagedUpdate(fx.state, fx.keys)); // same size, different bytes
  CHECK(fx.stage().error == StageError::None);
  CHECK(writeFileAtomic(joinPath(joinPath(pending, L"files"), L"LICENSE.txt"), "short"));
  CHECK(!loadStagedUpdate(fx.state, fx.keys)); // different size
  CHECK(fx.stage().error == StageError::None);
  CHECK(::DeleteFileW(joinPath(joinPath(pending, L"files"), L"LICENSE.txt").c_str()));
  CHECK(!loadStagedUpdate(fx.state, fx.keys)); // missing file
  CHECK(fx.stage().error == StageError::None);
  CHECK(::DeleteFileW(joinPath(pending, kReadyMarkerName).c_str()));
  CHECK(!loadStagedUpdate(fx.state, fx.keys)); // not ready
  CHECK(fx.stage().error == StageError::None);

  const std::wstring manifestPath = joinPath(pending, L"farsightvnc-update.manifest");
  CHECK(writeFileAtomic(manifestPath, fx.manifestText + "# tampered\n"));
  CHECK(!loadStagedUpdate(fx.state, fx.keys)); // signature no longer matches
  CHECK(fx.stage().error == StageError::None);
  std::string flipped = fx.signature;
  flipped[10] = static_cast<char>(flipped[10] ^ 0x01);
  CHECK(writeFileAtomic(joinPath(pending, L"farsightvnc-update.manifest.sig"), flipped));
  CHECK(!loadStagedUpdate(fx.state, fx.keys));
  CHECK(fx.stage().error == StageError::None);
  CHECK(writeFileAtomic(joinPath(pending, L"farsightvnc-update.manifest.sig"), "x"));
  CHECK(!loadStagedUpdate(fx.state, fx.keys)); // wrong signature size
  CHECK(fx.stage().error == StageError::None);
  CHECK(::DeleteFileW(manifestPath.c_str()));
  CHECK(!loadStagedUpdate(fx.state, fx.keys));

  // Nothing staged at all.
  CHECK(!loadStagedUpdate(temp.child(L"never-created"), fx.keys));
  CHECK(!loadStagedUpdate(std::wstring(), fx.keys));
}

void testStageAssetErrors(const TempDir &temp) {
  { // no progress callback is fine; missing assets
    StageFixture fx;
    fx.state = newState(temp);
    fx.omitManifest = true;
    fx.publish();
    expectFailure(fx, StageError::MissingAssets, "no manifest");
    CHECK(fx.http.requests.empty());
  }
  {
    StageFixture fx;
    fx.state = newState(temp);
    fx.omitSignature = true;
    fx.publish();
    expectFailure(fx, StageError::MissingAssets, "no signature");
    CHECK(fx.http.requests.empty());
  }
  {
    StageFixture fx;
    fx.state = newState(temp);
    fx.omitPackage = true;
    fx.publish();
    expectFailure(fx, StageError::MissingAssets, "no package");
  }
  {
    StageFixture fx;
    fx.state = newState(temp);
    fx.baseUrl = "https://evil.example/TheSampaio/FarSightVNC/releases/download/v0.3.0a/";
    fx.publish();
    expectFailure(fx, StageError::BadAssetUrl, "other host");
    CHECK(fx.http.requests.empty());
  }
  {
    StageFixture fx;
    fx.state = newState(temp);
    fx.baseUrl = std::string(kAssetUrlPrefix) + "v0.2.0a/";
    fx.publish();
    expectFailure(fx, StageError::BadAssetUrl, "other tag");
  }
  {
    StageFixture fx;
    fx.state = newState(temp);
    fx.baseUrl = std::string(kAssetUrlPrefix) + "v0.3.0a/../v0.2.0a/";
    fx.publish();
    expectFailure(fx, StageError::BadAssetUrl, "traversal");
  }
  { // only the package URL is off: caught after the manifest was verified
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    for (ReleaseAsset &asset : fx.release.assets) {
      if (asset.name == "FarSightVNC-v0.3.0a.zip") {
        asset.downloadUrl = "https://example.invalid/FarSightVNC-v0.3.0a.zip";
      }
    }
    expectFailure(fx, StageError::BadAssetUrl, "package host");
    CHECK(fx.http.countRequests("https://example.invalid/FarSightVNC-v0.3.0a.zip") == 0);
  }
  { // query string appended to a real asset
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.release.assets[0].downloadUrl += "?x=1";
    expectFailure(fx, StageError::BadAssetUrl, "query");
  }
}

void testStageVerification(const TempDir &temp) {
  { // production keys reject a test-signed manifest end to end
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    const StageResult result =
        stageUpdate(fx.http, fx.release, versionOf("0.2.1a"), productionKeys(), fx.state, {});
    CHECK(result.error == StageError::BadSignature);
    CHECK(fx.http.countRequests(fx.packageUrl) == 0);
    CHECK(pendingGone(fx));
  }
  { // signed by someone else
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    const TestKey other;
    fx.keys.clear();
    fx.keys.push_back(TrustedKey{7, other.publicBlob()});
    expectFailure(fx, StageError::BadSignature, "other key");
    CHECK(fx.http.countRequests(fx.packageUrl) == 0);
  }
  { // manifest altered after signing
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.manifestUrl].body += "file evil.exe 1 " + std::string(64, '0') + "\n";
    expectFailure(fx, StageError::BadSignature, "tampered manifest");
  }
  { // signature of the wrong length
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.signatureUrl].body.pop_back();
    expectFailure(fx, StageError::BadSignature, "short signature");
    fx.publish();
    fx.http.routes[fx.signatureUrl].body += 'x';
    expectFailure(fx, StageError::BadSignature, "long signature");
    fx.publish();
    fx.http.routes[fx.signatureUrl].body.clear();
    expectFailure(fx, StageError::BadSignature, "empty signature");
  }
  { // signed but not a manifest
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    const std::string junk = "this is not a manifest\n";
    const std::vector<std::uint8_t> sig = fx.key.sign(junk);
    fx.http.routes[fx.manifestUrl].body = junk;
    fx.http.routes[fx.signatureUrl].body.assign(sig.begin(), sig.end());
    expectFailure(fx, StageError::Malformed, "garbage manifest");
  }
  { // manifest larger than the limit
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.manifestUrl].body.assign(kMaxManifestBytes + 1, 'x');
    expectFailure(fx, StageError::TooLarge, "huge manifest");
  }
  { // signed manifest of another release replayed under this tag
    StageFixture fx;
    fx.state = newState(temp);
    fx.tagVersion = "0.3.1a";
    fx.manifestVersion = "0.3.0a";
    fx.publish();
    expectFailure(fx, StageError::VersionMismatch, "replayed manifest");
    CHECK(fx.http.countRequests(fx.packageUrl) == 0);
  }
  { // not newer: equal and older
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    expectFailure(fx, StageError::NotNewer, "equal version", "0.3.0a");
    expectFailure(fx, StageError::NotNewer, "older manifest", "0.3.1a");
    expectFailure(fx, StageError::NotNewer, "stable beats alpha", "0.3.0");
    CHECK(fx.http.countRequests(fx.packageUrl) == 0);
    CHECK(fx.stage("0.3.0a").error == StageError::NotNewer);
    CHECK(fx.stage("0.2.9").error == StageError::None); // numeric, not lexical
  }
}

void testStageDownload(const TempDir &temp) {
  { // transient network errors
    for (const HttpError error : {HttpError::Offline, HttpError::Timeout, HttpError::Tls,
                                  HttpError::BadStatus, HttpError::RateLimited}) {
      StageFixture fx;
      fx.state = newState(temp);
      fx.publish();
      fx.http.routes[fx.manifestUrl] = errorRoute(error, 500);
      const StageResult result = fx.stage();
      CHECK(result.error == StageError::Network);
      CHECK(result.httpError == error);
      CHECK(pendingGone(fx));
    }
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.signatureUrl] = errorRoute(HttpError::Timeout);
    CHECK(fx.stage().error == StageError::Network);
    fx.publish();
    fx.http.routes[fx.packageUrl] = errorRoute(HttpError::Offline);
    const StageResult result = fx.stage();
    CHECK(result.error == StageError::Network);
    CHECK(result.httpError == HttpError::Offline);
    CHECK(pendingGone(fx));
  }
  { // cancelled by the HTTP layer
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.manifestUrl] = errorRoute(HttpError::Cancelled);
    const StageResult result = fx.stage();
    CHECK(result.error == StageError::Cancelled);
    CHECK(result.httpError == HttpError::Cancelled);
    CHECK(pendingGone(fx));
  }
  { // connection lost half-way through the package
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.packageUrl].interruptAfter = 512;
    const StageResult result = fx.stage();
    CHECK(result.error == StageError::Network);
    CHECK(result.httpError == HttpError::Offline);
    CHECK(pendingGone(fx));
    CHECK(!loadStagedUpdate(fx.state, fx.keys));
  }
  { // cancelled by the user through the progress callback
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    int calls = 0;
    const StageResult result = fx.stage("0.2.1a", [&](std::uint64_t, std::uint64_t) {
      return ++calls < 3;
    });
    CHECK(result.error == StageError::Cancelled);
    CHECK(calls == 3);
    CHECK(pendingGone(fx));
    CHECK(!loadStagedUpdate(fx.state, fx.keys));
  }
  { // a package that differs from the signed hash (same size, one byte)
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    std::string &body = fx.http.routes[fx.packageUrl].body;
    body[body.size() / 2] = static_cast<char>(body[body.size() / 2] ^ 0x5A);
    expectFailure(fx, StageError::IntegrityFailed, "flipped byte");
  }
  { // truncated package delivered as a complete one
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.packageUrl].body.resize(fx.zip.size() - 10);
    expectFailure(fx, StageError::IntegrityFailed, "short package");
  }
  { // package larger than the signed size
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    fx.http.routes[fx.packageUrl].body += "padding";
    expectFailure(fx, StageError::IntegrityFailed, "long package");
  }
  { // valid zip whose contents differ from the signed file hashes
    StageFixture fx;
    fx.state = newState(temp);
    FileList wrong = fx.files;
    wrong[0].second = contentOf("evil", 5000); // same size, different bytes
    fx.zipFiles = wrong;
    fx.publish();
    expectFailure(fx, StageError::ExtractFailed, "content mismatch");
  }
  { // zip lacking a listed file
    StageFixture fx;
    fx.state = newState(temp);
    FileList missing = fx.files;
    missing.pop_back();
    fx.zipFiles = missing;
    fx.publish();
    expectFailure(fx, StageError::ExtractFailed, "missing entry");
  }
  { // the package is not a zip at all (hash matches, so the zip layer rejects it)
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    const std::string junk(2000, 'z');
    // Build a signed manifest that pins the junk package.
    const std::string text = "FarSightVNC-Update-Manifest 1\nversion 0.3.0a\npackage "
                             "FarSightVNC-v0.3.0a.zip " + std::to_string(junk.size()) + " " +
                             toHex(sha256(asBytes(junk))) + "\nfile LICENSE.txt 13 " +
                             toHex(sha256(asBytes(fx.files[2].second))) + "\n";
    const std::vector<std::uint8_t> sig = fx.key.sign(text);
    fx.http.routes[fx.manifestUrl].body = text;
    fx.http.routes[fx.signatureUrl].body.assign(sig.begin(), sig.end());
    fx.http.routes[fx.packageUrl].body = junk;
    expectFailure(fx, StageError::ExtractFailed, "not a zip");
  }
  { // a stale pending directory is wiped, even if staging then fails early
    StageFixture fx;
    fx.state = newState(temp);
    fx.publish();
    CHECK(fx.stage().error == StageError::None);
    CHECK(writeFileAtomic(joinPath(fx.pending(), L"stale.txt"), "old"));
    fx.http.routes[fx.manifestUrl] = errorRoute(HttpError::Offline);
    CHECK(fx.stage().error == StageError::Network);
    CHECK(pendingGone(fx));
    CHECK(!loadStagedUpdate(fx.state, fx.keys));
  }
  { // an empty state directory is refused
    StageFixture fx;
    fx.publish();
    fx.state.clear();
    CHECK(fx.stage().error == StageError::Io);
  }
  fsv::test::note("free-space failure (StageError::NoSpace) is not simulated: no seam for a fake disk");
}

} // namespace

void runUpdateServiceTests() {
  fsv::test::beginSuite("update-service");
  const TempDir temp;
  CHECK(!temp.path().empty());
  if (temp.path().empty()) {
    return;
  }
  const TestKey probe;
  CHECK(probe.valid());
  testCheck();
  testPolicy();
  testGuardedCheck();
  testUpdaterCommandLine();
  testInstallOutcome(temp);
  testSettings();
  testStageSuccess(temp);
  testStageAssetErrors(temp);
  testStageVerification(temp);
  testStageDownload(temp);
}
