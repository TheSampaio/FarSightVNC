// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Minimal ZIP extraction driven by the signed manifest. Output paths come from
// the manifest (already validated by isSafeRelativePath), never from archive
// entry names, so archive path traversal cannot reach the file system.

#pragma once

#include "Manifest.h"

namespace fsv::update {

// Extracts every file listed in manifest.files from zipPath into
// destRoot\<path> (creating subdirectories). Archive entry names are matched
// case-insensitively after converting '\\' to '/'. Requirements, otherwise
// false + error text and no guarantee about partially written files (callers
// extract into a fresh staging directory):
//  - classic ZIP only (no ZIP64, no encryption, no multi-disk), methods 0/8;
//  - each listed file present exactly once; unlisted entries are ignored;
//  - inflated size never exceeds the manifest size (zip-bomb bound), and
//    CRC-32, size and SHA-256 all match the manifest.
bool extractManifestFiles(const std::wstring &zipPath, const Manifest &manifest,
                          const std::wstring &destRoot, std::string *error);

} // namespace fsv::update
