// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
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
//-------------------------------------------------------------------------
//

#ifndef __RFB_MSG_DEFS_H_INCLUDED__
#define __RFB_MSG_DEFS_H_INCLUDED__

#include "util/inttypes.h"

class ClientMsgDefs
{
public:
  static const UINT32 SET_PIXEL_FORMAT = 0;
  static const UINT32 FIX_COLOR_MAP_ENTRIES = 1;
  static const UINT32 SET_ENCODINGS = 2;
  static const UINT32 FB_UPDATE_REQUEST = 3;
  static const UINT32 KEYBOARD_EVENT = 4;
  static const UINT32 POINTER_EVENT = 5;
  static const UINT32 CLIENT_CUT_TEXT = 6;
  static const UINT32 CLIENT_CUT_TEXT_UTF8 = 0xFC000200;
  static const UINT32 ENABLE_CUT_TEXT_UTF8 = 0xFC000201;
  static const UINT32 ECHO_REQUEST = 0xFC000300;
  // Modified for FarSightVNC on 2026-11-16: clipboard file paste, see
  // ClipboardFilesDefs below for the wire format.
  static const UINT32 CLIPBOARD_FILES_BEGIN = 0xFC000400;
  static const UINT32 CLIPBOARD_FILES_SET = 0xFC000401;
};

class ServerMsgDefs
{
public:
  static const UINT32 FB_UPDATE = 0;
  static const UINT32 SET_COLOR_MAP_ENTRIES = 1;
  static const UINT32 BELL = 2;
  static const UINT32 SERVER_CUT_TEXT = 3;
  static const UINT32 SERVER_CUT_TEXT_UTF8 = 0xFC000200;
  static const UINT32 ECHO_RESPONSE = 0xFC000300;
  // Modified for FarSightVNC on 2026-11-16: clipboard file paste.
  static const UINT32 CLIPBOARD_FILES_REPLY = 0xFC000400;
};

class Utf8CutTextDefs
{
public: 
    static const char *const CLIENT_CUT_TEXT_UTF8_SIG;
    static const char *const SERVER_CUT_TEXT_UTF8_SIG;
    static const char *const ENABLE_CUT_TEXT_UTF8_SIG;
};
class EchoExtensionDefs
{
public:
  static const char *const ECHO_REQUEST_SIG;
  static const char *const ECHO_RESPONSE_SIG;
};

// Modified for FarSightVNC on 2026-11-16: clipboard file paste.
//
// FarSightVNC clipboard file paste, local viewer machine -> remote server
// machine only (there is no remote -> local direction).
//
// Three messages in the free 0xFC0004xx range. They are advertised with the
// FarSight vendor signature (VendorDefs::FARSIGHT, "FSVC"), never with
// "TGHT": a capability is only enabled when the code AND the vendor AND the
// name signature match, so a stock TightVNC viewer or server can never
// mistake these messages for one of its own.
//
// Strings are length-prefixed UTF-8 (UINT32 size in bytes followed by the
// bytes, as DataOutputStream::writeUTF8 writes them and as the file transfer
// extension does, see ft-common/FTMessage.h). Lists are preceded by their
// UINT32 element count, so fields can be appended to a message later without
// breaking a peer that only knows the fields before them.
//
// CLIPBOARD_FILES_BEGIN (client -> server, 0xFC000400)
//   no body.
//   The server creates a per-paste staging folder in the temp area of the
//   interactive user and answers with CLIPBOARD_FILES_REPLY, kind REPLY_ROOT.
//
// CLIPBOARD_FILES_SET (client -> server, 0xFC000401)
//   StringUTF8 stagingRoot        absolute Windows path of the staging folder
//                                 the server handed out in REPLY_ROOT.
//   UINT32     pathCount          number of staged entries.
//   StringUTF8 path[pathCount]    absolute Windows path of each staged entry;
//                                 every path must be stagingRoot, a backslash
//                                 and exactly one name component.
//   The server validates the list (see ClipboardFilesMessage::checkPaths),
//   puts a CF_HDROP holding it on the clipboard of the interactive user and
//   answers CLIPBOARD_FILES_REPLY, kind REPLY_SET.
//
// CLIPBOARD_FILES_REPLY (server -> client, 0xFC000400)
//   UINT8      kind               1 = REPLY_ROOT, 2 = REPLY_SET.
//   UINT8      status             0 = ok, 1 = failed.
//   StringUTF8 text               the staging folder when kind is REPLY_ROOT
//                                 and status is 0, otherwise the description
//                                 of the failure (may be empty).
class ClipboardFilesDefs
{
public:
  static const char *const CLIPBOARD_FILES_BEGIN_SIG;
  static const char *const CLIPBOARD_FILES_SET_SIG;
  static const char *const CLIPBOARD_FILES_REPLY_SIG;
};
#endif // __RFB_MSG_DEFS_H_INCLUDED__
