// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// HTTPS client abstraction. The production implementation is WinHTTP with
// system certificate validation (never disabled), system/corporate proxy
// settings, redirects restricted to HTTPS, and bounded timeouts and sizes.
// Tests inject a fake IHttpClient.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fsv::update {

enum class HttpError {
  None,
  BadUrl,       // not https:// or unparsable
  Offline,      // name resolution / connection failure
  Timeout,
  Tls,          // certificate or secure channel failure
  BadStatus,    // non-2xx (status in HttpResponse::status)
  RateLimited,  // 403/429 with GitHub rate-limit headers
  TooLarge,     // body exceeded maxBytes or Content-Length too big
  Cancelled,
  Io,           // local file write failure
  Other,
};

const char *httpErrorName(HttpError error);

struct HttpResponse {
  std::uint32_t status = 0;
  std::string body;                   // get() only
  std::uint64_t contentLength = 0;    // 0 if unknown
  std::uint64_t rateLimitResetUnix = 0; // X-RateLimit-Reset when present
};

using Headers = std::vector<std::pair<std::string, std::string>>;
// (bytesReceived, totalBytesOrZero) -> false to cancel.
using ProgressFn = std::function<bool(std::uint64_t, std::uint64_t)>;

class IHttpClient {
public:
  virtual ~IHttpClient() = default;
  virtual HttpError get(const std::string &url, const Headers &headers,
                        std::size_t maxBytes, HttpResponse &out) = 0;
  // Streams the body to destPath (created/truncated). On any error the
  // partial file is deleted.
  virtual HttpError download(const std::string &url, const std::wstring &destPath,
                             std::uint64_t maxBytes, const ProgressFn &progress,
                             HttpResponse &out) = 0;
  // Thread-safe; aborts an in-flight request as soon as possible.
  virtual void cancel() = 0;
};

// userAgent example: "FarSightVNC-Updater/0.3.0a" (GitHub requires one; no
// other identifying data is sent).
std::unique_ptr<IHttpClient> createWinHttpClient(const std::wstring &userAgent);

} // namespace fsv::update
