// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "FsUtil.h"

#include "Crypto.h"
#include "Win32Util.h"

#include <bcrypt.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <winver.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "version.lib")

namespace fsv::update {

namespace {

constexpr std::size_t kMaxRemoveDepth = 64;
constexpr DWORD kWriteChunk = 1u << 20;

bool isSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

// Removes trailing separators but keeps a drive root such as "C:\" intact.
std::wstring trimTrailingSeparators(std::wstring path) {
  while (!path.empty() && isSeparator(path.back())) {
    if (path.size() == 3 && path[1] == L':') {
      break; // "C:\"
    }
    path.pop_back();
  }
  return path;
}

bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  if (a.empty()) {
    return true;
  }
  return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// Eight random bytes as 16 hex characters (system RNG; process/clock based
// fallback, which is still safe because every use creates the file with
// CREATE_NEW).
std::wstring randomHex16() {
  static std::atomic<std::uint64_t> counter{0};
  std::uint8_t bytes[8] = {};
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    std::uint64_t seed = ::GetTickCount64() ^ (static_cast<std::uint64_t>(::GetCurrentProcessId()) << 32) ^
                         (++counter * 0x9E3779B97F4A7C15ull);
    for (std::uint8_t &byte : bytes) {
      byte = static_cast<std::uint8_t>(seed & 0xFF);
      seed = (seed >> 8) | (seed << 56);
    }
  }
  static constexpr wchar_t kDigits[] = L"0123456789abcdef";
  std::wstring text;
  for (const std::uint8_t byte : bytes) {
    text += kDigits[byte >> 4];
    text += kDigits[byte & 0x0F];
  }
  return text;
}

// "\\?\"-prefixed form of an absolute, normalized path (no MAX_PATH limit).
std::wstring extendedPath(const std::wstring &fullPathText) {
  if (fullPathText.rfind(L"\\\\?\\", 0) == 0) {
    return fullPathText;
  }
  if (fullPathText.rfind(L"\\\\", 0) == 0) {
    return L"\\\\?\\UNC\\" + fullPathText.substr(2);
  }
  return L"\\\\?\\" + fullPathText;
}

bool isMissingError(DWORD error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
         error == ERROR_INVALID_NAME;
}

struct FindCloser {
  void operator()(HANDLE handle) const {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
      ::FindClose(handle);
    }
  }
};
using FindHandle = std::unique_ptr<void, FindCloser>;

// Deletes `path` (an extended-length absolute path). Directories are emptied
// first; a reparse point (junction, symlink) is removed itself, never followed.
bool deleteEntry(const std::wstring &path, std::size_t depth) {
  if (depth > kMaxRemoveDepth) {
    return false;
  }
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return isMissingError(::GetLastError());
  }
  if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
    ::SetFileAttributesW(path.c_str(), attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
  }

  const bool isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const bool isReparse = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  if (!isDirectory) {
    return ::DeleteFileW(path.c_str()) != FALSE || isMissingError(::GetLastError());
  }
  if (isReparse) {
    return ::RemoveDirectoryW(path.c_str()) != FALSE || isMissingError(::GetLastError());
  }

  std::vector<std::wstring> children;
  {
    WIN32_FIND_DATAW data{};
    FindHandle find(::FindFirstFileExW((path + L"\\*").c_str(), FindExInfoBasic, &data,
                                       FindExSearchNameMatch, nullptr,
                                       FIND_FIRST_EX_LARGE_FETCH));
    if (find.get() == INVALID_HANDLE_VALUE) {
      return false;
    }
    do {
      const std::wstring_view name = data.cFileName;
      if (name != L"." && name != L"..") {
        children.emplace_back(name);
      }
    } while (::FindNextFileW(find.get(), &data));
    if (::GetLastError() != ERROR_NO_MORE_FILES) {
      return false;
    }
  }

  bool ok = true;
  for (const std::wstring &child : children) {
    ok = deleteEntry(path + L"\\" + child, depth + 1) && ok;
  }
  if (!ok) {
    return false;
  }
  return ::RemoveDirectoryW(path.c_str()) != FALSE || isMissingError(::GetLastError());
}

// True if some existing component strictly between `base` and `full` is a
// reparse point, i.e. deleting below it would leave the guarded tree.
bool crossesReparsePoint(const std::wstring &base, const std::wstring &full) {
  std::size_t pos = base.size();
  while (true) {
    pos = full.find(L'\\', pos + 1);
    if (pos == std::wstring::npos) {
      return false; // the last component itself may be a link: it is deleted, not followed
    }
    const DWORD attributes = ::GetFileAttributesW(extendedPath(full.substr(0, pos)).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      return true;
    }
  }
}

bool writeAll(HANDLE file, std::string_view data) {
  const char *cursor = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0) {
    const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(remaining, kWriteChunk));
    DWORD written = 0;
    if (!::WriteFile(file, cursor, chunk, &written, nullptr) || written == 0) {
      return false;
    }
    cursor += written;
    remaining -= written;
  }
  return true;
}

bool queryVersionString(const std::vector<char> &block, const std::wstring &langCp,
                        std::optional<Version> &out) {
  const std::wstring subBlock = L"\\StringFileInfo\\" + langCp + L"\\ProductVersion";
  LPVOID value = nullptr;
  UINT length = 0;
  if (!::VerQueryValueW(block.data(), subBlock.c_str(), &value, &length) || value == nullptr ||
      length == 0) {
    return false;
  }
  const auto *text = static_cast<const wchar_t *>(value);
  const std::wstring product(text, ::wcsnlen(text, length));
  out = Version::parse(wideToUtf8(product));
  return out.has_value();
}

std::wstring hex4(WORD value) {
  static constexpr wchar_t kDigits[] = L"0123456789abcdef";
  std::wstring text(4, L'0');
  for (int i = 3; i >= 0; --i) {
    text[static_cast<std::size_t>(i)] = kDigits[value & 0xF];
    value = static_cast<WORD>(value >> 4);
  }
  return text;
}

} // namespace

// ------------------------------------------------------------- conversions

std::wstring utf8ToWide(std::string_view text) {
  if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return {};
  }
  const int size = static_cast<int>(text.size());
  const int needed =
      ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  return guarded(
      [&] {
        std::wstring result(static_cast<std::size_t>(needed), L'\0');
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, result.data(),
                              needed);
        return result;
      },
      std::wstring{});
}

std::string wideToUtf8(std::wstring_view text) {
  if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return {};
  }
  const int size = static_cast<int>(text.size());
  const int needed = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
                                           nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  return guarded(
      [&] {
        std::string result(static_cast<std::size_t>(needed), '\0');
        ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, result.data(),
                              needed, nullptr, nullptr);
        return result;
      },
      std::string{});
}

// ------------------------------------------------------------------- paths

std::wstring joinPath(const std::wstring &dir, const std::wstring &name) {
  return guarded(
      [&] {
        if (dir.empty()) {
          return name;
        }
        if (name.empty()) {
          return dir;
        }
        std::size_t end = dir.size();
        while (end > 0 && isSeparator(dir[end - 1])) {
          --end;
        }
        std::size_t begin = 0;
        while (begin < name.size() && isSeparator(name[begin])) {
          ++begin;
        }
        return dir.substr(0, end) + L'\\' + name.substr(begin);
      },
      std::wstring{});
}

std::wstring parentDirectory(const std::wstring &path) {
  return guarded(
      [&] {
        std::wstring trimmed = trimTrailingSeparators(path);
        if (trimmed.size() == 3 && trimmed[1] == L':') {
          return std::wstring{}; // a drive root has no parent
        }
        const std::size_t slash = trimmed.find_last_of(L"\\/");
        if (slash == std::wstring::npos) {
          return std::wstring{};
        }
        std::wstring parent = trimmed.substr(0, slash);
        if (parent.size() == 2 && parent[1] == L':') {
          parent += L'\\'; // keep the drive root ("C:\"); "C:" would be drive-relative
        }
        return parent;
      },
      std::wstring{});
}

std::wstring fileName(const std::wstring &path) {
  return guarded(
      [&] {
        const std::wstring trimmed = trimTrailingSeparators(path);
        const std::size_t slash = trimmed.find_last_of(L"\\/");
        return slash == std::wstring::npos ? trimmed : trimmed.substr(slash + 1);
      },
      std::wstring{});
}

std::wstring fullPath(const std::wstring &path) {
  return guarded(
      [&] {
        if (path.empty()) {
          return std::wstring{};
        }
        DWORD needed = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        while (needed != 0) {
          std::wstring buffer(needed, L'\0');
          const DWORD length =
              ::GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);
          if (length == 0) {
            break;
          }
          if (length < needed) {
            buffer.resize(length);
            return buffer;
          }
          needed = length + 1; // the path grew between the two calls
        }
        return path;
      },
      std::wstring{});
}

bool pathsEqual(const std::wstring &a, const std::wstring &b) {
  return guarded(
      [&] {
        return equalsIgnoreCase(trimTrailingSeparators(fullPath(a)),
                                trimTrailingSeparators(fullPath(b)));
      },
      false);
}

// ----------------------------------------------------------- file system

bool fileExists(const std::wstring &path) {
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool directoryExists(const std::wstring &path) {
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ensureDirectory(const std::wstring &path) {
  return guarded(
      [&] {
        if (path.empty()) {
          return false;
        }
        const std::wstring target = trimTrailingSeparators(fullPath(path));
        if (directoryExists(target)) {
          return true;
        }
        const std::wstring parent = parentDirectory(target);
        if (!parent.empty() && !equalsIgnoreCase(parent, target) && !ensureDirectory(parent)) {
          return false;
        }
        if (::CreateDirectoryW(target.c_str(), nullptr)) {
          return true;
        }
        return ::GetLastError() == ERROR_ALREADY_EXISTS && directoryExists(target);
      },
      false);
}

bool removeTree(const std::wstring &path, const std::wstring &mustBeUnder) {
  return guarded(
      [&] {
        if (path.empty() || mustBeUnder.empty()) {
          return false;
        }
        const std::wstring full = trimTrailingSeparators(fullPath(path));
        const std::wstring base = trimTrailingSeparators(fullPath(mustBeUnder));
        // A whole drive (or an empty/relative result) is never a useful guard.
        if (full.empty() || base.size() <= 3 || base.size() >= full.size()) {
          return false;
        }
        if (!equalsIgnoreCase(std::wstring_view(full).substr(0, base.size()), base) ||
            full[base.size()] != L'\\') {
          return false;
        }
        if (crossesReparsePoint(base, full)) {
          return false;
        }
        return deleteEntry(extendedPath(full), 0);
      },
      false);
}

std::optional<std::uint64_t> freeSpaceBytes(const std::wstring &dir) {
  ULARGE_INTEGER available{};
  if (!::GetDiskFreeSpaceExW(dir.c_str(), &available, nullptr, nullptr)) {
    return std::nullopt;
  }
  return available.QuadPart;
}

bool canWriteDirectory(const std::wstring &dir) {
  return guarded(
      [&] {
        if (!directoryExists(dir)) {
          return false;
        }
        const std::wstring probe = joinPath(dir, L".fsv-probe-" + randomHex16() + L".tmp");
        UniqueHandle file(::CreateFileW(probe.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                                        CREATE_NEW,
                                        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                                        nullptr));
        if (!file.valid()) {
          return false;
        }
        return writeAll(file.get(), std::string_view("x", 1));
      },
      false);
}

bool canReplaceFile(const std::wstring &file) {
  return guarded(
      [&] {
        if (!fileExists(file)) {
          return canWriteDirectory(parentDirectory(fullPath(file)));
        }
        UniqueHandle handle(::CreateFileW(
            file.c_str(), GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        return handle.valid();
      },
      false);
}

bool writeFileAtomic(const std::wstring &path, std::string_view data) {
  return guarded(
      [&] {
        if (path.empty()) {
          return false;
        }
        const std::wstring temp = path + L".tmp-" + randomHex16();
        bool written = false;
        {
          UniqueHandle file(::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                          FILE_ATTRIBUTE_NORMAL, nullptr));
          if (!file.valid()) {
            return false;
          }
          written = writeAll(file.get(), data) && ::FlushFileBuffers(file.get()) != FALSE;
        }
        if (written && ::MoveFileExW(temp.c_str(), path.c_str(),
                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
          return true;
        }
        ::DeleteFileW(temp.c_str());
        return false;
      },
      false);
}

std::optional<std::string> readFile(const std::wstring &path, std::uint64_t maxBytes) {
  return guarded(
      [&]() -> std::optional<std::string> {
        UniqueHandle file(::CreateFileW(path.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                        nullptr));
        if (!file.valid()) {
          return std::nullopt;
        }
        LARGE_INTEGER size{};
        if (!::GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
            static_cast<std::uint64_t>(size.QuadPart) > maxBytes) {
          return std::nullopt;
        }

        std::string content;
        content.reserve(static_cast<std::size_t>(size.QuadPart));
        char buffer[64 * 1024];
        for (;;) {
          DWORD read = 0;
          if (!::ReadFile(file.get(), buffer, sizeof(buffer), &read, nullptr)) {
            return std::nullopt;
          }
          if (read == 0) {
            break;
          }
          if (content.size() + read > maxBytes) {
            return std::nullopt; // the file grew while reading
          }
          content.append(buffer, read);
        }
        return content;
      },
      std::optional<std::string>{});
}

// -------------------------------------------------------- well-known paths

std::wstring currentModulePath() {
  return guarded(
      [] {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
          const DWORD length =
              ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
          if (length == 0) {
            return std::wstring{};
          }
          if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
          }
          if (buffer.size() >= 65536) {
            return std::wstring{};
          }
          buffer.resize(buffer.size() * 2);
        }
      },
      std::wstring{});
}

std::wstring localAppDataDirectory() {
  PWSTR raw = nullptr;
  const HRESULT result = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw);
  std::wstring path;
  if (SUCCEEDED(result) && raw != nullptr) {
    path = guarded([&] { return std::wstring(raw); }, std::wstring{});
  }
  ::CoTaskMemFree(raw);
  return path;
}

std::wstring updateStateDirectory(const std::wstring &installRoot) {
  return guarded(
      [&] {
        const std::wstring localAppData = localAppDataDirectory();
        if (localAppData.empty() || installRoot.empty()) {
          return std::wstring{};
        }
        std::wstring root = trimTrailingSeparators(fullPath(installRoot));
        if (!root.empty()) {
          ::CharLowerBuffW(root.data(), static_cast<DWORD>(root.size()));
        }
        const std::string utf8 = wideToUtf8(root);
        const Sha256Digest digest = sha256(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(utf8.data()), utf8.size()));
        const std::wstring hash = utf8ToWide(toHex(digest).substr(0, 16));
        return joinPath(joinPath(joinPath(localAppData, L"FarSightVNC"), L"Updates"), hash);
      },
      std::wstring{});
}

std::optional<Version> readExecutableVersion(const std::wstring &exePath) {
  return guarded(
      [&]() -> std::optional<Version> {
        DWORD handle = 0;
        const DWORD size = ::GetFileVersionInfoSizeW(exePath.c_str(), &handle);
        if (size == 0) {
          return std::nullopt;
        }
        std::vector<char> block(size);
        if (!::GetFileVersionInfoW(exePath.c_str(), 0, size, block.data())) {
          return std::nullopt;
        }

        std::optional<Version> version;
        LPVOID translations = nullptr;
        UINT translationBytes = 0;
        if (::VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", &translations,
                             &translationBytes) &&
            translations != nullptr) {
          struct LangAndCodePage {
            WORD language;
            WORD codePage;
          };
          const auto *entries = static_cast<const LangAndCodePage *>(translations);
          const std::size_t count = translationBytes / sizeof(LangAndCodePage);
          for (std::size_t i = 0; i < count; ++i) {
            if (queryVersionString(block, hex4(entries[i].language) + hex4(entries[i].codePage),
                                   version)) {
              return version;
            }
          }
        }
        if (queryVersionString(block, L"040904b0", version)) {
          return version;
        }
        return std::nullopt;
      },
      std::optional<Version>{});
}

} // namespace fsv::update
