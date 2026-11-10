// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// updater-test: deterministic tests of updater-lib. Exit code 0 = all passed.

#include "TestHarness.h"

#include "updater-lib/Crypto.h"
#include "updater-lib/FsUtil.h"
#include "updater-lib/UpdateService.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

void runVersionTests();
void runCryptoTests();
void runFsUtilTests();
void runJsonTests();
void runReleasesTests();
void runManifestTests();
void runZipTests();
void runUpdateServiceTests();
void runInstallerTests();
void runHttpTests();

namespace {

void runSuite(void (*suite)()) {
  try {
    suite();
  } catch (const std::exception &error) {
    fsv::test::recordResult(false, "suite threw an exception", __FILE__, __LINE__);
    std::printf("  exception: %s\n", error.what());
  } catch (...) {
    fsv::test::recordResult(false, "suite threw an unknown exception", __FILE__, __LINE__);
  }
}

// IHttpClient for --stage-local: maps release asset URLs to local files. No
// network access whatsoever.
class LocalFileHttpClient final : public fsv::update::IHttpClient {
public:
  void add(const std::string &url, const std::wstring &path) { m_files[url] = path; }

  fsv::update::HttpError get(const std::string &url, const fsv::update::Headers &, std::size_t maxBytes,
                             fsv::update::HttpResponse &out) override {
    const auto it = m_files.find(url);
    if (it == m_files.end()) {
      return fsv::update::HttpError::BadStatus;
    }
    std::ifstream in(std::filesystem::path(it->second), std::ios::binary);
    if (!in) {
      return fsv::update::HttpError::Io;
    }
    out.body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.body.size() > maxBytes) {
      out.body.clear();
      return fsv::update::HttpError::TooLarge;
    }
    out.status = 200;
    out.contentLength = out.body.size();
    return fsv::update::HttpError::None;
  }

  fsv::update::HttpError download(const std::string &url, const std::wstring &destPath,
                                  std::uint64_t maxBytes, const fsv::update::ProgressFn &progress,
                                  fsv::update::HttpResponse &out) override {
    const auto it = m_files.find(url);
    if (it == m_files.end()) {
      return fsv::update::HttpError::BadStatus;
    }
    std::error_code ec;
    const std::uint64_t size = std::filesystem::file_size(std::filesystem::path(it->second), ec);
    if (ec) {
      return fsv::update::HttpError::Io;
    }
    if (size > maxBytes) {
      return fsv::update::HttpError::TooLarge;
    }
    if (progress && !progress(0, size)) {
      return fsv::update::HttpError::Cancelled;
    }
    if (!::CopyFileW(it->second.c_str(), destPath.c_str(), FALSE)) {
      ::DeleteFileW(destPath.c_str());
      return fsv::update::HttpError::Io;
    }
    if (progress && !progress(size, size)) {
      ::DeleteFileW(destPath.c_str());
      return fsv::update::HttpError::Cancelled;
    }
    out.status = 200;
    out.contentLength = size;
    return fsv::update::HttpError::None;
  }

  void cancel() override {}

private:
  std::map<std::string, std::wstring> m_files;
};

// updater-test.exe --stage-local <tag> <dir-with-assets> <stateDir> <currentVersion>
// Runs the PRODUCTION staging path (compiled-in keys) on the manifest,
// signature and zip found in <dir-with-assets>. Exit 0 only on StageError::None.
int runStageLocal(int argc, wchar_t **argv) {
  using namespace fsv::update;
  if (argc != 6) {
    std::fputs("usage: updater-test --stage-local <tag> <assets-dir> <stateDir> <currentVersion>\n",
               stderr);
    return 2;
  }
  const std::string tag = wideToUtf8(argv[2]);
  const std::filesystem::path dir = argv[3];
  const std::wstring stateDir = argv[4];
  const std::optional<Version> tagVersion = Version::parse(tag);
  const std::optional<Version> current = Version::parse(wideToUtf8(argv[5]));
  if (!tagVersion || !current) {
    std::fputs("invalid tag or current version\n", stderr);
    return 2;
  }

  LocalFileHttpClient http;
  ReleaseInfo release;
  release.tag = tag;
  release.version = *tagVersion;
  release.githubPrerelease = tagVersion->isPrerelease();
  std::vector<std::filesystem::path> names{kManifestAssetName, kSignatureAssetName};
  // The package is "FarSightVNC-<tag>.zip"; any other *.zip in the folder is ignored.
  const std::filesystem::path packageName = "FarSightVNC-" + tag + ".zip";
  if (std::filesystem::is_regular_file(dir / packageName)) {
    names.push_back(packageName);
  }
  for (const std::filesystem::path &name : names) {
    ReleaseAsset asset;
    asset.name = wideToUtf8(name.wstring());
    asset.downloadUrl = std::string(kAssetUrlPrefix) + tag + "/" + asset.name;
    std::error_code ec;
    asset.size = std::filesystem::file_size(dir / name, ec);
    http.add(asset.downloadUrl, (dir / name).wstring());
    release.assets.push_back(std::move(asset));
  }

  const StageResult result = stageUpdate(http, release, *current, productionKeys(), stateDir,
                                         [](std::uint64_t, std::uint64_t) { return true; });
  std::printf("StageError: %s (%s)\n", stageErrorName(result.error), result.detail.c_str());
  std::printf("state dir: %ls\n", stateDir.c_str());
  return result.error == StageError::None ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
  // updater-test.exe --state-dir <installRoot>: prints FsUtil::updateStateDirectory(root).
  if (argc == 3 && std::strcmp(argv[1], "--state-dir") == 0) {
    int wideCount = 0;
    wchar_t **wide = ::CommandLineToArgvW(::GetCommandLineW(), &wideCount);
    if (wide == nullptr || wideCount != 3) {
      return 2;
    }
    std::printf("%ls\n", fsv::update::updateStateDirectory(wide[2]).c_str());
    ::LocalFree(wide);
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "--stage-local") == 0) {
    int wideCount = 0;
    wchar_t **wide = ::CommandLineToArgvW(::GetCommandLineW(), &wideCount);
    if (wide == nullptr) {
      return 2;
    }
    const int code = runStageLocal(wideCount, wide);
    ::LocalFree(wide);
    return code;
  }
  // Helper mode: copies of this exe act as fake "fsvncviewer.exe" processes.
  if (argc >= 3 && std::strcmp(argv[1], "--sleep-ms") == 0) {
    ::Sleep(static_cast<DWORD>(std::atol(argv[2])));
    return 0;
  }
  runSuite(runVersionTests);
  runSuite(runCryptoTests);
  runSuite(runFsUtilTests);
  runSuite(runJsonTests);
  runSuite(runReleasesTests);
  runSuite(runManifestTests);
  runSuite(runZipTests);
  runSuite(runUpdateServiceTests);
  runSuite(runInstallerTests);
  runSuite(runHttpTests);

  const fsv::test::Counters &counters = fsv::test::counters();
  std::printf("updater-test: %d suites, %d checks, %d failures, %d notes\n", counters.suites,
              counters.checks, counters.failures, counters.notes);
  return counters.failures == 0 ? 0 : 1;
}
