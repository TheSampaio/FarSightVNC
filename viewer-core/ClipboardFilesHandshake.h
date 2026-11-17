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

#ifndef _CLIPBOARD_FILES_HANDSHAKE_H_
#define _CLIPBOARD_FILES_HANDSHAKE_H_

#include "ClipboardFilesCapability.h"

#include "log-writer/LogWriter.h"
#include "thread/LocalMutex.h"
#include "util/CommonHeader.h"
#include "util/StringStorage.h"
#include "win-system/WindowsEvent.h"

#include <vector>

// The two request/reply exchanges of a clipboard file paste with the server:
// the staging folder (CLIPBOARD_FILES_BEGIN) and the staged file list
// (CLIPBOARD_FILES_SET). It has no window and no file transfer in it, so that
// the handshake can be tested against a fake server.
//
// Both calls block the calling thread (the staging thread of the paste, never
// the thread that reads the connection) until the server answers, 'cancel' is
// signalled or the time runs out.
//
// A reply is only taken when the matching request is outstanding. An
// unsolicited or repeated reply, or a reply that arrives after a timeout, is
// ignored: it can neither change the staging folder that was handed out nor
// release a paste that the server did not confirm. Since the protocol has no
// request IDs, a timeout or cancellation retires this handshake until the
// session is reconnected: an old reply must never satisfy a new paste.
class ClipboardFilesHandshake : public ClipboardFilesListener
{
public:
  // Registers itself as the listener of 'capability' and unregisters in the
  // destructor.
  ClipboardFilesHandshake(ClipboardFilesCapability *capability,
                          LogWriter *logWriter);
  virtual ~ClipboardFilesHandshake();

  // Sends CLIPBOARD_FILES_BEGIN and waits for the staging folder. True only
  // when the server answered with a folder that is a well formed staging root
  // (ClipboardFilesMessage::isStagingRoot); 'root' is then set. Otherwise
  // 'error' says why, and nothing was or will be pasted.
  // 'cancel' may be 0.
  bool requestStagingRoot(HANDLE cancel, DWORD timeoutMs,
                          StringStorage *root, StringStorage *error);

  // Sends CLIPBOARD_FILES_SET for the entries 'names' of the staging folder
  // 'root' and waits for the confirmation. True only when the server
  // confirmed that its clipboard holds the files, which is the moment the
  // keystroke may be released to the remote application.
  bool announceStagedFiles(const StringStorage *root,
                           const std::vector<StringStorage> *names,
                           HANDLE cancel, DWORD timeoutMs,
                           StringStorage *error);

  //
  // Inherited from ClipboardFilesListener (remote viewer core thread).
  //
  virtual void onClipboardFilesRoot(bool ok, const StringStorage *text);
  virtual void onClipboardFilesSet(bool ok, const StringStorage *text);

private:
  enum Awaiting { AWAIT_NONE, AWAIT_ROOT, AWAIT_SET };

  // Marks the reply of 'kind' as expected. Must be called before the request
  // is sent, so that a quick reply is not taken for an unsolicited one.
  bool beginAwaiting(Awaiting kind, StringStorage *error);
  // Waits for the reply, then stops expecting one. False when it was
  // cancelled or timed out; otherwise the reply is in 'ok' and 'text'.
  bool awaitReply(HANDLE cancel, DWORD timeoutMs, bool *ok, StringStorage *text);
  void onReply(Awaiting kind, bool ok, const StringStorage *text);

  ClipboardFilesCapability *m_capability;
  LogWriter *m_logWriter;

  mutable LocalMutex m_mutex;
  Awaiting m_awaiting;
  bool m_replyOk;
  bool m_desynchronized;
  StringStorage m_replyText;
  WindowsEvent m_replyEvent;
};

#endif // _CLIPBOARD_FILES_HANDSHAKE_H_
