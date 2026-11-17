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

#include "ClipboardFilesHandshake.h"

#include "thread/AutoLock.h"

ClipboardFilesHandshake::ClipboardFilesHandshake(ClipboardFilesCapability *capability,
                                                 LogWriter *logWriter)
: m_capability(capability),
  m_logWriter(logWriter),
  m_awaiting(AWAIT_NONE),
  m_replyOk(false),
  m_desynchronized(false)
{
  m_capability->setListener(this);
}

ClipboardFilesHandshake::~ClipboardFilesHandshake()
{
  m_capability->setListener(0);
}

bool ClipboardFilesHandshake::beginAwaiting(Awaiting kind, StringStorage *error)
{
  AutoLock al(&m_mutex);
  if (m_desynchronized) {
    error->setString(_T("Reconnect before pasting files again after a cancelled or unanswered request."));
    return false;
  }
  m_awaiting = kind;
  m_replyOk = false;
  m_replyText.setString(_T(""));
  // The event is auto-reset, but a signal left by a reply that raced a
  // timeout would end this wait at once.
  WaitForSingleObject(m_replyEvent.getHandle(), 0);
  return true;
}

bool ClipboardFilesHandshake::awaitReply(HANDLE cancel, DWORD timeoutMs,
                                         bool *ok, StringStorage *text)
{
  HANDLE handles[2] = { m_replyEvent.getHandle(), cancel };
  DWORD count = cancel != 0 ? 2 : 1;
  DWORD result = WaitForMultipleObjects(count, handles, FALSE, timeoutMs);

  AutoLock al(&m_mutex);
  bool answered = result == WAIT_OBJECT_0;
  m_awaiting = AWAIT_NONE; // whatever arrives from now on is not ours
  if (!answered) {
    m_desynchronized = true;
    return false;
  }
  *ok = m_replyOk;
  *text = m_replyText;
  return true;
}

void ClipboardFilesHandshake::onReply(Awaiting kind, bool ok,
                                      const StringStorage *text)
{
  {
    AutoLock al(&m_mutex);
    if (m_awaiting != kind) {
      m_logWriter->info(_T("Clipboard file paste: unexpected %s reply ignored"),
                        kind == AWAIT_ROOT ? _T("staging folder")
                                           : _T("file list"));
      return;
    }
    m_awaiting = AWAIT_NONE; // one reply per request
    m_replyOk = ok;
    m_replyText = *text;
    // Publish the reply and its signal under the same lock, so a timed-out
    // waiter cannot begin another request before this signal exists.
    m_replyEvent.notify();
  }
}

void ClipboardFilesHandshake::onClipboardFilesRoot(bool ok,
                                                   const StringStorage *text)
{
  onReply(AWAIT_ROOT, ok, text);
}

void ClipboardFilesHandshake::onClipboardFilesSet(bool ok,
                                                  const StringStorage *text)
{
  onReply(AWAIT_SET, ok, text);
}

bool ClipboardFilesHandshake::requestStagingRoot(HANDLE cancel, DWORD timeoutMs,
                                                 StringStorage *root,
                                                 StringStorage *error)
{
  if (!m_capability->isSupported()) {
    error->setString(_T("The remote computer does not support pasting files."));
    return false;
  }

  if (!beginAwaiting(AWAIT_ROOT, error)) {
    return false;
  }
  m_capability->sendBegin();

  bool ok = false;
  StringStorage text;
  if (!awaitReply(cancel, timeoutMs, &ok, &text)) {
    error->setString(_T("The remote computer did not answer the paste request."));
    return false;
  }
  if (!ok) {
    error->format(_T("The remote computer refused the paste: %s"),
                  text.getString());
    return false;
  }
  // The server is trusted to create the folder, not to name one anywhere:
  // the files are uploaded into it and announced from it.
  if (!ClipboardFilesMessage::isStagingRoot(&text)) {
    m_logWriter->error(_T("Clipboard file paste: the server named a staging")
                       _T(" folder that is not a staging root: \"%s\""),
                       text.getString());
    error->setString(_T("The remote computer named an invalid staging folder."));
    return false;
  }
  *root = text;
  return true;
}

bool ClipboardFilesHandshake::announceStagedFiles(const StringStorage *root,
                                                  const std::vector<StringStorage> *names,
                                                  HANDLE cancel, DWORD timeoutMs,
                                                  StringStorage *error)
{
  if (!m_capability->isSupported()) {
    error->setString(_T("The remote computer does not support pasting files."));
    return false;
  }

  ClipboardFilesMessage message;
  message.setRoot(root->getString());
  for (size_t i = 0; i < names->size(); i++) {
    StringStorage staged(root->getString());
    staged.appendChar(_T('\\'));
    staged.appendString((*names)[i].getString());
    message.addPath(staged.getString());
  }

  if (!beginAwaiting(AWAIT_SET, error)) {
    return false;
  }
  m_capability->sendSet(&message);

  bool ok = false;
  StringStorage text;
  if (!awaitReply(cancel, timeoutMs, &ok, &text)) {
    error->setString(_T("The remote computer did not confirm the paste."));
    return false;
  }
  if (!ok) {
    error->format(_T("The remote computer could not set its clipboard: %s"),
                  text.getString());
    return false;
  }
  return true;
}
