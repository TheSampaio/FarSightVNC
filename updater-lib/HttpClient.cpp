// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "HttpClient.h"

namespace fsv::update {

// STUB(worker): names of the HttpError values are not written yet.
const char *httpErrorName(HttpError) { return "unimplemented"; }

// STUB(worker): the WinHTTP client is not written yet (link winhttp.lib here
// with #pragma comment when it is).
std::unique_ptr<IHttpClient> createWinHttpClient(const std::wstring &) { return nullptr; }

} // namespace fsv::update
