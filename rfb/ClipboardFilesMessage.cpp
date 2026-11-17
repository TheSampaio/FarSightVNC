// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a modified version of TightVNC.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Added for FarSightVNC on 2026-11-16: clipboard file paste (local viewer
// machine -> remote server machine).
//

#include "rfb/ClipboardFilesMessage.h"
#include "util/Exception.h"

namespace {

// Names of the two fixed components of a staging root, as
// ClipboardStaging (server side) builds them.
const TCHAR STAGING_PRODUCT_DIR[] = _T("FarSightVNC");
const TCHAR STAGING_CLIPBOARD_DIR[] = _T("Clipboard");

TCHAR toLower(TCHAR c)
{
  if (c >= _T('A') && c <= _T('Z')) {
    return static_cast<TCHAR>(c - _T('A') + _T('a'));
  }
  return c;
}

bool equalsNoCase(const TCHAR *a, const TCHAR *b)
{
  for (; *a != 0 && *b != 0; a++, b++) {
    if (toLower(*a) != toLower(*b)) {
      return false;
    }
  }
  return *a == *b;
}

bool isAsciiLetter(TCHAR c)
{
  return (c >= _T('a') && c <= _T('z')) || (c >= _T('A') && c <= _T('Z'));
}

bool isTokenChar(TCHAR c)
{
  return isAsciiLetter(c) || (c >= _T('0') && c <= _T('9')) ||
         c == _T('-') || c == _T('_');
}

// A single name component that may be handed to the shell: not empty, not
// "." or "..", and free of the characters Windows forbids in a name (the
// path separators and ':' among them) and of control characters.
bool isSafeNameComponent(const TCHAR *name, size_t length)
{
  if (length == 0) {
    return false;
  }
  if (equalsNoCase(name, _T(".")) || equalsNoCase(name, _T(".."))) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    TCHAR c = name[i];
    if (c < 0x20 || c == _T('\\') || c == _T('/') || c == _T(':') ||
        c == _T('<') || c == _T('>') || c == _T('"') || c == _T('|') ||
        c == _T('?') || c == _T('*')) {
      return false;
    }
  }
  // A trailing space or dot makes Windows resolve the name to another file.
  TCHAR last = name[length - 1];
  return last != _T(' ') && last != _T('.');
}

} // namespace

ClipboardFilesMessage::ClipboardFilesMessage()
{
}

void ClipboardFilesMessage::setRoot(const TCHAR *root)
{
  m_root.setString(root);
}

void ClipboardFilesMessage::addPath(const TCHAR *path)
{
  m_paths.push_back(StringStorage(path));
}

void ClipboardFilesMessage::clear()
{
  m_root.setString(_T(""));
  m_paths.clear();
}

void ClipboardFilesMessage::writeSetBody(DataOutputStream *output) const
{
  output->writeUTF8(m_root.getString());
  output->writeUInt32(static_cast<UINT32>(m_paths.size()));
  for (size_t i = 0; i < m_paths.size(); i++) {
    output->writeUTF8(m_paths[i].getString());
  }
}

void ClipboardFilesMessage::readSetBody(DataInputStream *input)
{
  clear();
  input->readUTF8(&m_root);
  UINT32 count = input->readUInt32();
  if (count > MAX_PATH_COUNT) {
    StringStorage message;
    message.format(_T("Clipboard file paste: %u paths requested, at most %u")
                   _T(" are accepted"),
                   static_cast<unsigned>(count),
                   static_cast<unsigned>(MAX_PATH_COUNT));
    throw Exception(message.getString());
  }
  m_paths.resize(count);
  for (UINT32 i = 0; i < count; i++) {
    input->readUTF8(&m_paths[i]);
  }
}

void ClipboardFilesMessage::writeReplyBody(DataOutputStream *output,
                                           UINT8 kind, UINT8 status,
                                           const TCHAR *text)
{
  output->writeUInt8(kind);
  output->writeUInt8(status);
  output->writeUTF8(text);
}

void ClipboardFilesMessage::readReplyBody(DataInputStream *input, UINT8 *kind,
                                          UINT8 *status, StringStorage *text)
{
  *kind = input->readUInt8();
  *status = input->readUInt8();
  input->readUTF8(text);
}

ClipboardFilesMessage::CheckResult ClipboardFilesMessage::check() const
{
  return checkPaths(&m_root, &m_paths);
}

bool ClipboardFilesMessage::isStagingRoot(const StringStorage *root)
{
  const TCHAR *path = root->getString();
  size_t length = root->getLength();
  if (length < 4 || length > MAX_PATH_LENGTH) {
    return false;
  }
  // "<drive>:\": absolute, local, not UNC and not a "\\?\" device path.
  if (!isAsciiLetter(path[0]) || path[1] != _T(':') || path[2] != _T('\\')) {
    return false;
  }
  if (path[length - 1] == _T('\\')) {
    return false;
  }
  // Split everything after the drive into components and keep the last
  // three, which must be "FarSightVNC\Clipboard\<token>".
  StringStorage components[3];
  size_t componentCount = 0;
  size_t start = 3;
  for (size_t i = 3; i <= length; i++) {
    if (i != length && path[i] != _T('\\') && path[i] != _T('/')) {
      continue;
    }
    if (path[i] == _T('/')) {
      return false; // only backslashes in a Windows staging root
    }
    size_t componentLength = i - start;
    if (componentLength == 0) {
      return false; // an empty component ("\\" inside the path)
    }
    StringStorage component;
    root->getSubstring(&component, start, i - 1);
    if (component.isEqualTo(_T(".")) || component.isEqualTo(_T(".."))) {
      return false;
    }
    components[componentCount % 3] = component;
    componentCount++;
    start = i + 1;
  }
  if (componentCount < 3) {
    return false;
  }
  // components[] is a ring buffer of the last three components.
  const StringStorage &product = components[(componentCount - 3) % 3];
  const StringStorage &clipboard = components[(componentCount - 2) % 3];
  const StringStorage &token = components[(componentCount - 1) % 3];
  if (!equalsNoCase(product.getString(), STAGING_PRODUCT_DIR) ||
      !equalsNoCase(clipboard.getString(), STAGING_CLIPBOARD_DIR)) {
    return false;
  }
  size_t tokenLength = token.getLength();
  if (tokenLength == 0 || tokenLength > MAX_TOKEN_LENGTH) {
    return false;
  }
  for (size_t i = 0; i < tokenLength; i++) {
    if (!isTokenChar(token.getString()[i])) {
      return false;
    }
  }
  return true;
}

ClipboardFilesMessage::CheckResult
ClipboardFilesMessage::checkPaths(const StringStorage *root,
                                  const std::vector<StringStorage> *paths)
{
  if (!isStagingRoot(root)) {
    return CHECK_BAD_ROOT;
  }
  if (paths->empty()) {
    return CHECK_EMPTY;
  }
  if (paths->size() > MAX_PATH_COUNT) {
    return CHECK_TOO_MANY;
  }

  const TCHAR *rootPath = root->getString();
  size_t rootLength = root->getLength();
  size_t totalLength = rootLength;

  for (size_t i = 0; i < paths->size(); i++) {
    const StringStorage &path = (*paths)[i];
    size_t pathLength = path.getLength();
    if (pathLength > MAX_PATH_LENGTH) {
      return CHECK_TOO_LONG;
    }
    if (totalLength > MAX_TOTAL_LENGTH - pathLength) {
      return CHECK_TOO_LONG;
    }
    totalLength += pathLength;
    if (totalLength > MAX_TOTAL_LENGTH) {
      return CHECK_TOO_LONG;
    }

    const TCHAR *value = path.getString();
    // The path must begin with the root and a separator. Anything else is
    // outside the folder the server handed out, including a relative path
    // and a path on another drive.
    if (pathLength <= rootLength + 1) {
      return CHECK_OUTSIDE_ROOT;
    }
    for (size_t j = 0; j < rootLength; j++) {
      if (toLower(value[j]) != toLower(rootPath[j])) {
        return CHECK_OUTSIDE_ROOT;
      }
    }
    if (value[rootLength] != _T('\\')) {
      return CHECK_OUTSIDE_ROOT;
    }

    const TCHAR *name = value + rootLength + 1;
    size_t nameLength = pathLength - rootLength - 1;
    // A ".." anywhere in the remainder would climb out of the root even
    // though the prefix matches; it is reported on its own so that the log
    // says what a hostile peer tried.
    for (size_t j = 0; j + 1 < nameLength; j++) {
      if (name[j] == _T('.') && name[j + 1] == _T('.')) {
        return CHECK_TRAVERSAL;
      }
    }
    if (!isSafeNameComponent(name, nameLength)) {
      return CHECK_BAD_NAME;
    }
  }
  return CHECK_OK;
}

const TCHAR *ClipboardFilesMessage::describe(CheckResult result)
{
  switch (result) {
  case CHECK_OK:
    return _T("ok");
  case CHECK_EMPTY:
    return _T("the path list is empty");
  case CHECK_TOO_MANY:
    return _T("too many paths");
  case CHECK_TOO_LONG:
    return _T("the path list is too long");
  case CHECK_BAD_ROOT:
    return _T("the staging folder is not a FarSightVNC clipboard folder");
  case CHECK_OUTSIDE_ROOT:
    return _T("a path is outside the staging folder");
  case CHECK_TRAVERSAL:
    return _T("a path contains \"..\"");
  case CHECK_BAD_NAME:
    return _T("a path is not a single valid file name");
  }
  return _T("unknown error");
}
