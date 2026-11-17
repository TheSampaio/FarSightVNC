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

#ifndef __RFB_CLIPBOARD_FILES_MESSAGE_H_INCLUDED__
#define __RFB_CLIPBOARD_FILES_MESSAGE_H_INCLUDED__

#include "io-lib/DataInputStream.h"
#include "io-lib/DataOutputStream.h"
#include "util/StringStorage.h"
#include "util/inttypes.h"

#include <vector>

// Body of the CLIPBOARD_FILES_SET and CLIPBOARD_FILES_REPLY messages (the
// wire format is documented next to the message codes in rfb/MsgDefs.h) and
// the rules a received path list must satisfy.
//
// Everything here is plain string and stream work: no Windows API and no file
// system access, so the server can check a request before anything touches
// the shell, and so the rules can be tested without two machines.
class ClipboardFilesMessage
{
public:
  // Kind field of CLIPBOARD_FILES_REPLY.
  static const UINT8 REPLY_ROOT = 1;
  static const UINT8 REPLY_SET = 2;

  // Status field of CLIPBOARD_FILES_REPLY.
  static const UINT8 STATUS_OK = 0;
  static const UINT8 STATUS_FAILED = 1;

  // Limits of a path list. They bound what a peer can make the server
  // allocate and what it can hand to the shell.
  static const size_t MAX_PATH_COUNT = 1024;
  // Windows long path maximum.
  static const size_t MAX_PATH_LENGTH = 32767;
  // Characters of the staging root plus every path together.
  static const size_t MAX_TOTAL_LENGTH = 1024 * 1024;
  // Characters of the unique name of a staging folder.
  static const size_t MAX_TOKEN_LENGTH = 64;

  // Why a path list was refused. OK means the list may be used.
  enum CheckResult {
    CHECK_OK = 0,
    CHECK_EMPTY,
    CHECK_TOO_MANY,
    CHECK_TOO_LONG,
    CHECK_BAD_ROOT,
    CHECK_OUTSIDE_ROOT,
    CHECK_TRAVERSAL,
    CHECK_BAD_NAME
  };

  ClipboardFilesMessage();

  void setRoot(const TCHAR *root);
  const StringStorage *getRoot() const { return &m_root; }

  void addPath(const TCHAR *path);
  const std::vector<StringStorage> *getPaths() const { return &m_paths; }

  void clear();

  // CLIPBOARD_FILES_SET body. The message code itself is written and read by
  // the caller, which is the side that knows the output gate and the
  // dispatcher.
  void writeSetBody(DataOutputStream *output) const;
  // Throws when a length field is over the limits, so that a hostile peer
  // cannot make the reader allocate without bound. The path list itself is
  // only checked by check().
  void readSetBody(DataInputStream *input);

  // CLIPBOARD_FILES_REPLY body.
  static void writeReplyBody(DataOutputStream *output, UINT8 kind,
                             UINT8 status, const TCHAR *text);
  static void readReplyBody(DataInputStream *input, UINT8 *kind,
                            UINT8 *status, StringStorage *text);

  // Checks the root and the paths of this message.
  CheckResult check() const;

  // Rules of a path list:
  //   * the root is a well formed staging root (see isStagingRoot);
  //   * there is at least one path and at most MAX_PATH_COUNT of them;
  //   * every path is the root, a backslash and exactly one name component
  //     (so nothing outside the root, no nested path, no "." and no "..");
  //   * no path is longer than MAX_PATH_LENGTH and the whole list is not
  //     longer than MAX_TOTAL_LENGTH characters.
  // The root prefix is compared case insensitively, as Windows paths are.
  static CheckResult checkPaths(const StringStorage *root,
                                const std::vector<StringStorage> *paths);

  // True when the path is "<drive>:\...\FarSightVNC\Clipboard\<token>": an
  // absolute local path (no UNC and no "\\?\" device path), without a "."
  // or ".." component, whose last three components are the FarSightVNC
  // clipboard staging layout and whose unique name holds only letters,
  // digits, '-' and '_'.
  static bool isStagingRoot(const StringStorage *root);

  // One line for the log and for the CLIPBOARD_FILES_REPLY text.
  static const TCHAR *describe(CheckResult result);

private:
  StringStorage m_root;
  std::vector<StringStorage> m_paths;
};

#endif // __RFB_CLIPBOARD_FILES_MESSAGE_H_INCLUDED__
