// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// WinHTTP implementation of IHttpClient. Synchronous calls; certificate
// validation is never relaxed, redirects may not downgrade to http, cookies
// and implicit credentials are off, and every body is bounded.

#include "HttpClient.h"

#include "FsUtil.h"
#include "Win32Util.h"

#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <mutex>

#pragma comment(lib, "winhttp.lib")

// Not defined by the SDK headers when targeting Windows 7.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace fsv::update {

const char *httpErrorName(HttpError error) {
  switch (error) {
  case HttpError::None: return "none";
  case HttpError::BadUrl: return "bad-url";
  case HttpError::Offline: return "offline";
  case HttpError::Timeout: return "timeout";
  case HttpError::Tls: return "tls";
  case HttpError::BadStatus: return "bad-status";
  case HttpError::RateLimited: return "rate-limited";
  case HttpError::TooLarge: return "too-large";
  case HttpError::Cancelled: return "cancelled";
  case HttpError::Io: return "io";
  case HttpError::Other: return "other";
  }
  return "unknown";
}

namespace {

constexpr std::size_t kMaxUrlChars = 4096;
constexpr DWORD kReadChunkBytes = 64 * 1024;
constexpr DWORD kMaxRedirects = 10;

struct InternetCloser {
  void operator()(HINTERNET handle) const {
    if (handle != nullptr) {
      ::WinHttpCloseHandle(handle);
    }
  }
};
using InternetHandle = std::unique_ptr<void, InternetCloser>;

HttpError mapWinHttpError(DWORD code) {
  switch (code) {
  case ERROR_WINHTTP_NAME_NOT_RESOLVED:
  case ERROR_WINHTTP_CANNOT_CONNECT:
  case ERROR_WINHTTP_CONNECTION_ERROR:
    return HttpError::Offline;
  case ERROR_WINHTTP_TIMEOUT:
    return HttpError::Timeout;
  case ERROR_WINHTTP_SECURE_FAILURE:
  case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
  case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
  case ERROR_WINHTTP_SECURE_INVALID_CA:
  case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
  case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
  case ERROR_WINHTTP_SECURE_INVALID_CERT:
  case ERROR_WINHTTP_SECURE_CERT_REVOKED:
  case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
  case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
    return HttpError::Tls;
  case ERROR_WINHTTP_OPERATION_CANCELLED:
    return HttpError::Cancelled;
  case ERROR_WINHTTP_INVALID_URL:
  case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
  case ERROR_WINHTTP_REDIRECT_FAILED:
    return HttpError::BadUrl;
  default:
    return HttpError::Other;
  }
}

bool startsWithHttps(const std::wstring &url) {
  static constexpr wchar_t kPrefix[] = L"https://";
  constexpr std::size_t length = sizeof(kPrefix) / sizeof(kPrefix[0]) - 1;
  return url.size() >= length && ::_wcsnicmp(url.c_str(), kPrefix, length) == 0;
}

bool isHeaderToken(const std::string &text, bool allowSpaceAndColon) {
  for (const char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F || (!allowSpaceAndColon && (c == ':' || c == ' '))) {
      return false;
    }
  }
  return true;
}

// "Name: value\r\n" for each header, or nullopt if one is malformed (a CR/LF
// or colon would allow header injection).
std::optional<std::wstring> buildHeaderBlock(const Headers &headers) {
  std::string block;
  for (const auto &[name, value] : headers) {
    if (name.empty() || !isHeaderToken(name, false) || !isHeaderToken(value, true)) {
      return std::nullopt;
    }
    block += name + ": " + value + "\r\n";
  }
  return utf8ToWide(block);
}

std::optional<std::uint64_t> parseUnsigned(const std::wstring &text) {
  if (text.empty() || text.size() > 19) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const wchar_t c : text) {
    if (c < L'0' || c > L'9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<std::uint64_t>(c - L'0');
  }
  return value;
}

std::optional<std::wstring> queryHeaderText(HINTERNET request, DWORD level, const wchar_t *name) {
  DWORD bytes = 0;
  ::WinHttpQueryHeaders(request, level, name, WINHTTP_NO_OUTPUT_BUFFER, &bytes,
                        WINHTTP_NO_HEADER_INDEX);
  if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0 || bytes > 8192) {
    return std::nullopt;
  }
  std::wstring text(bytes / sizeof(wchar_t) + 1, L'\0');
  if (!::WinHttpQueryHeaders(request, level, name, text.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) {
    return std::nullopt;
  }
  text.resize(bytes / sizeof(wchar_t));
  while (!text.empty() && text.back() == L'\0') {
    text.pop_back();
  }
  return text;
}

std::optional<std::wstring> customHeader(HINTERNET request, const wchar_t *name) {
  return queryHeaderText(request, WINHTTP_QUERY_CUSTOM, name);
}

std::uint64_t nowUnix() { return static_cast<std::uint64_t>(std::time(nullptr)); }

// GitHub signals a rate limit with 403/429 plus X-RateLimit-Remaining: 0 or
// Retry-After. Returns true and the reset time (0 if unknown) when it does.
bool detectRateLimit(HINTERNET request, std::uint32_t status, std::uint64_t &resetUnix) {
  if (status != 403 && status != 429) {
    return false;
  }
  const std::optional<std::wstring> remaining = customHeader(request, L"X-RateLimit-Remaining");
  const std::optional<std::wstring> retryAfter = customHeader(request, L"Retry-After");
  const bool exhausted = remaining && parseUnsigned(*remaining) == std::optional<std::uint64_t>(0);
  if (!exhausted && !retryAfter) {
    return false;
  }
  resetUnix = 0;
  if (const std::optional<std::wstring> reset = customHeader(request, L"X-RateLimit-Reset")) {
    resetUnix = parseUnsigned(*reset).value_or(0);
  }
  if (resetUnix == 0 && retryAfter) {
    if (const std::optional<std::uint64_t> seconds = parseUnsigned(*retryAfter)) {
      resetUnix = nowUnix() + std::min<std::uint64_t>(*seconds, 7 * 24 * 3600);
    }
  }
  return true;
}

// Receives the body. onBegin runs once after a 2xx status with the announced
// size (0 = unknown); onData runs per chunk. Either may fail the request.
struct BodySink {
  std::function<HttpError(std::uint64_t total)> onBegin;
  std::function<HttpError(const char *data, DWORD size, std::uint64_t received,
                          std::uint64_t total)> onData;
};

class WinHttpClient final : public IHttpClient {
public:
  explicit WinHttpClient(const std::wstring &userAgent) {
    HINTERNET session = ::WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
      // Windows older than 8.1: default proxy plus explicit PAC/WPAD lookups.
      m_manualProxyLookup = true;
      session = ::WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == nullptr) {
      return;
    }
    m_session.reset(session);
    // Certificate validation stays at the system default: never touch
    // WINHTTP_OPTION_SECURITY_FLAGS.
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!::WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                            sizeof(protocols))) {
      protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
      ::WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    ::WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
  }

  bool valid() const { return m_session != nullptr; }

  HttpError get(const std::string &url, const Headers &headers, std::size_t maxBytes,
                HttpResponse &out) override {
    return guarded(
        [&] {
          out = HttpResponse{};
          BodySink sink;
          sink.onData = [&](const char *data, DWORD size, std::uint64_t, std::uint64_t) {
            out.body.append(data, size);
            return HttpError::None;
          };
          return perform(url, headers, maxBytes, out, sink);
        },
        HttpError::Other);
  }

  HttpError download(const std::string &url, const std::wstring &destPath,
                     std::uint64_t maxBytes, const ProgressFn &progress,
                     HttpResponse &out) override {
    return guarded(
        [&] {
          out = HttpResponse{};
          UniqueHandle file;
          BodySink sink;
          sink.onBegin = [&](std::uint64_t total) {
            file.reset(::CreateFileW(destPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!file.valid()) {
              return HttpError::Io;
            }
            return progress && !progress(0, total) ? HttpError::Cancelled : HttpError::None;
          };
          sink.onData = [&](const char *data, DWORD size, std::uint64_t received,
                            std::uint64_t total) {
            DWORD written = 0;
            if (!::WriteFile(file.get(), data, size, &written, nullptr) || written != size) {
              return HttpError::Io;
            }
            return progress && !progress(received, total) ? HttpError::Cancelled
                                                          : HttpError::None;
          };
          const HttpError error = perform(url, Headers{}, maxBytes, out, sink);
          file.reset();
          if (error != HttpError::None) {
            ::DeleteFileW(destPath.c_str());
          }
          return error;
        },
        HttpError::Other);
  }

  // Sticky: once cancelled, this client fails every later request with
  // Cancelled. Use one client per operation.
  void cancel() override {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled = true;
    if (m_inflight != nullptr) {
      ::WinHttpCloseHandle(std::exchange(m_inflight, nullptr));
    }
  }

private:
  // Makes the in-flight request handle visible to cancel(); the destructor
  // closes it unless cancel() already did.
  class RequestSlot {
  public:
    RequestSlot(WinHttpClient &owner, HINTERNET request) : m_owner(owner), m_request(request) {
      std::lock_guard<std::mutex> lock(owner.m_mutex);
      if (owner.m_cancelled) {
        ::WinHttpCloseHandle(request);
        m_request = nullptr;
      } else {
        owner.m_inflight = request;
      }
    }
    ~RequestSlot() {
      if (m_request == nullptr) {
        return;
      }
      std::lock_guard<std::mutex> lock(m_owner.m_mutex);
      if (m_owner.m_inflight == m_request) {
        m_owner.m_inflight = nullptr;
        ::WinHttpCloseHandle(m_request);
      }
    }
    RequestSlot(const RequestSlot &) = delete;
    RequestSlot &operator=(const RequestSlot &) = delete;
    bool active() const { return m_request != nullptr; }

  private:
    WinHttpClient &m_owner;
    HINTERNET m_request;
  };

  bool cancelled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cancelled;
  }

  HttpError failure(DWORD lastError) const {
    return cancelled() ? HttpError::Cancelled : mapWinHttpError(lastError);
  }

  void applyManualProxy(HINTERNET request, const std::wstring &url) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie{};
    if (!::WinHttpGetIEProxyConfigForCurrentUser(&ie)) {
      return;
    }
    WINHTTP_PROXY_INFO info{};
    bool have = false;
    if (ie.fAutoDetect || ie.lpszAutoConfigUrl != nullptr) {
      WINHTTP_AUTOPROXY_OPTIONS options{};
      if (ie.lpszAutoConfigUrl != nullptr) {
        options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
        options.lpszAutoConfigUrl = ie.lpszAutoConfigUrl;
      } else {
        options.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
        options.dwAutoDetectFlags =
            WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
      }
      options.fAutoLogonIfChallenged = FALSE;
      have = ::WinHttpGetProxyForUrl(m_session.get(), url.c_str(), &options, &info) != FALSE;
    }
    if (have) {
      ::WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &info, sizeof(info));
      if (info.lpszProxy != nullptr) {
        ::GlobalFree(info.lpszProxy);
      }
      if (info.lpszProxyBypass != nullptr) {
        ::GlobalFree(info.lpszProxyBypass);
      }
    } else if (ie.lpszProxy != nullptr) {
      WINHTTP_PROXY_INFO named{};
      named.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
      named.lpszProxy = ie.lpszProxy;
      named.lpszProxyBypass = ie.lpszProxyBypass;
      ::WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &named, sizeof(named));
    }
    if (ie.lpszProxy != nullptr) {
      ::GlobalFree(ie.lpszProxy);
    }
    if (ie.lpszProxyBypass != nullptr) {
      ::GlobalFree(ie.lpszProxyBypass);
    }
    if (ie.lpszAutoConfigUrl != nullptr) {
      ::GlobalFree(ie.lpszAutoConfigUrl);
    }
  }

  HttpError perform(const std::string &url, const Headers &headers, std::uint64_t maxBytes,
                    HttpResponse &out, BodySink &sink) {
    if (!valid()) {
      return HttpError::Other;
    }
    if (cancelled()) {
      return HttpError::Cancelled;
    }
    if (url.empty() || url.size() > kMaxUrlChars) {
      return HttpError::BadUrl;
    }
    const std::optional<std::wstring> headerBlock = buildHeaderBlock(headers);
    if (!headerBlock) {
      return HttpError::BadUrl;
    }

    const std::wstring wideUrl = utf8ToWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = static_cast<DWORD>(-1);
    parts.dwPasswordLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (wideUrl.empty() ||
        !::WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwHostNameLength == 0 ||
        parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) {
      return HttpError::BadUrl;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring target(parts.lpszUrlPath, parts.dwUrlPathLength);
    target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (target.empty()) {
      target = L"/";
    }

    InternetHandle connection(::WinHttpConnect(m_session.get(), host.c_str(), parts.nPort, 0));
    if (!connection) {
      return failure(::GetLastError());
    }
    HINTERNET rawRequest = ::WinHttpOpenRequest(connection.get(), L"GET", target.c_str(), nullptr,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
    if (rawRequest == nullptr) {
      return failure(::GetLastError());
    }
    RequestSlot slot(*this, rawRequest);
    if (!slot.active()) {
      return HttpError::Cancelled;
    }
    const HINTERNET request = rawRequest;

    DWORD feature = WINHTTP_DISABLE_COOKIES;
    ::WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    ::WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon));
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    ::WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                       sizeof(redirectPolicy));
    DWORD maxRedirects = kMaxRedirects;
    ::WinHttpSetOption(request, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &maxRedirects,
                       sizeof(maxRedirects));
    if (m_manualProxyLookup) {
      applyManualProxy(request, wideUrl);
    }

    if (!::WinHttpSendRequest(request, headerBlock->empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                            : headerBlock->c_str(),
                              static_cast<DWORD>(headerBlock->size()),
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !::WinHttpReceiveResponse(request, nullptr)) {
      return failure(::GetLastError());
    }

    // The final URL after redirects must still be https.
    DWORD urlBytes = 0;
    ::WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &urlBytes);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || urlBytes == 0 ||
        urlBytes > kMaxUrlChars * 4 * sizeof(wchar_t)) {
      return failure(::GetLastError());
    }
    std::wstring finalUrl(urlBytes / sizeof(wchar_t) + 1, L'\0');
    if (!::WinHttpQueryOption(request, WINHTTP_OPTION_URL, finalUrl.data(), &urlBytes)) {
      return failure(::GetLastError());
    }
    finalUrl.resize(urlBytes / sizeof(wchar_t));
    if (!startsWithHttps(finalUrl)) {
      return HttpError::BadUrl;
    }

    DWORD status = 0;
    DWORD statusBytes = sizeof(status);
    if (!::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes,
                               WINHTTP_NO_HEADER_INDEX)) {
      return failure(::GetLastError());
    }
    out.status = status;
    if (status < 200 || status > 299) {
      std::uint64_t resetUnix = 0;
      if (detectRateLimit(request, status, resetUnix)) {
        out.rateLimitResetUnix = resetUnix;
        return HttpError::RateLimited;
      }
      return HttpError::BadStatus;
    }

    bool lengthKnown = false;
    if (const std::optional<std::wstring> text =
            queryHeaderText(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX)) {
      if (const std::optional<std::uint64_t> length = parseUnsigned(*text)) {
        out.contentLength = *length;
        lengthKnown = true;
      }
    }
    if (lengthKnown && out.contentLength > maxBytes) {
      return HttpError::TooLarge;
    }

    if (sink.onBegin) {
      if (const HttpError error = sink.onBegin(out.contentLength); error != HttpError::None) {
        return error;
      }
    }
    std::vector<char> buffer(kReadChunkBytes);
    std::uint64_t received = 0;
    for (;;) {
      if (cancelled()) {
        return HttpError::Cancelled;
      }
      DWORD available = 0;
      if (!::WinHttpQueryDataAvailable(request, &available)) {
        return failure(::GetLastError());
      }
      if (available == 0) {
        break;
      }
      const DWORD want = std::min(available, kReadChunkBytes);
      DWORD got = 0;
      if (!::WinHttpReadData(request, buffer.data(), want, &got)) {
        return failure(::GetLastError());
      }
      if (got == 0) {
        break;
      }
      if (received + got > maxBytes) {
        return HttpError::TooLarge;
      }
      received += got;
      if (const HttpError error = sink.onData(buffer.data(), got, received, out.contentLength);
          error != HttpError::None) {
        return error;
      }
    }
    // A connection dropped mid-body can look like a normal end of data.
    if (lengthKnown && received != out.contentLength) {
      return HttpError::Offline;
    }
    return HttpError::None;
  }

  InternetHandle m_session;
  bool m_manualProxyLookup = false;
  mutable std::mutex m_mutex;
  HINTERNET m_inflight = nullptr; // guarded by m_mutex
  bool m_cancelled = false;       // guarded by m_mutex
};

} // namespace

std::unique_ptr<IHttpClient> createWinHttpClient(const std::wstring &userAgent) {
  return guarded(
      [&]() -> std::unique_ptr<IHttpClient> {
        auto client = std::make_unique<WinHttpClient>(userAgent);
        if (!client->valid()) {
          return nullptr;
        }
        return client;
      },
      std::unique_ptr<IHttpClient>());
}

} // namespace fsv::update
