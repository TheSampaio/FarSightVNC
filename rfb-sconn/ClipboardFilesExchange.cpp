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

#include "ClipboardFilesExchange.h"

#include "network/RfbInputGate.h"
#include "rfb/MsgDefs.h"
#include "rfb/VendorDefs.h"
#include "server-config-lib/Configurator.h"
#include "thread/AutoLock.h"
#include "util/Exception.h"

ClipboardFilesExchange::ClipboardFilesExchange(RfbCodeRegistrator *codeRegtor,
                                               Desktop *desktop,
                                               RfbOutputGate *output,
                                               bool enabled,
                                               LogWriter *log)
: m_desktop(desktop),
  m_output(output),
  m_enabled(enabled),
  m_log(log)
{
  if (!m_enabled) {
    m_log->info(_T("Clipboard file paste is not allowed"));
    return;
  }

  codeRegtor->addClToSrvCap(ClientMsgDefs::CLIPBOARD_FILES_BEGIN,
                            VendorDefs::FARSIGHT,
                            ClipboardFilesDefs::CLIPBOARD_FILES_BEGIN_SIG);
  codeRegtor->addClToSrvCap(ClientMsgDefs::CLIPBOARD_FILES_SET,
                            VendorDefs::FARSIGHT,
                            ClipboardFilesDefs::CLIPBOARD_FILES_SET_SIG);
  codeRegtor->addSrvToClCap(ServerMsgDefs::CLIPBOARD_FILES_REPLY,
                            VendorDefs::FARSIGHT,
                            ClipboardFilesDefs::CLIPBOARD_FILES_REPLY_SIG);

  codeRegtor->regCode(ClientMsgDefs::CLIPBOARD_FILES_BEGIN, this);
  codeRegtor->regCode(ClientMsgDefs::CLIPBOARD_FILES_SET, this);

  m_log->message(_T("Clipboard file paste handler created"));
}

ClipboardFilesExchange::~ClipboardFilesExchange()
{
}

bool ClipboardFilesExchange::isEnabled() const
{
  return m_enabled &&
         Configurator::getInstance()->getServerConfig()->isFileTransfersEnabled();
}

void ClipboardFilesExchange::onRequest(UINT32 reqCode, RfbInputGate *input)
{
  switch (reqCode) {
  case ClientMsgDefs::CLIPBOARD_FILES_BEGIN:
    onBeginRequest();
    break;
  case ClientMsgDefs::CLIPBOARD_FILES_SET:
    onSetRequest(input);
    break;
  default:
    StringStorage errMess;
    errMess.format(_T("Unknown %d protocol code received by the clipboard")
                   _T(" file paste handler"), (int)reqCode);
    throw Exception(errMess.getString());
  }
}

void ClipboardFilesExchange::onBeginRequest()
{
  if (!isEnabled()) {
    m_log->info(_T("Clipboard file paste refused: file transfer is disabled"));
    sendReply(ClipboardFilesMessage::REPLY_ROOT,
              ClipboardFilesMessage::STATUS_FAILED,
              _T("File transfer is disabled on the server"));
    return;
  }

  StringStorage root;
  m_desktop->getClipboardStagingRoot(&root);

  // The folder is created in the session of the interactive user, so the
  // server itself checks that what came back is the staging layout before it
  // hands the path to the viewer and later accepts it again.
  if (root.isEmpty() || !ClipboardFilesMessage::isStagingRoot(&root)) {
    m_log->error(_T("Clipboard file paste: no usable staging folder (\"%s\")"),
                 root.getString());
    {
      AutoLock al(&m_stagingRootMutex);
      m_stagingRoot.setString(_T(""));
    }
    sendReply(ClipboardFilesMessage::REPLY_ROOT,
              ClipboardFilesMessage::STATUS_FAILED,
              _T("Could not create a staging folder on the server"));
    return;
  }

  {
    AutoLock al(&m_stagingRootMutex);
    m_stagingRoot = root;
  }
  m_log->info(_T("Clipboard file paste: staging folder handed out"));
  sendReply(ClipboardFilesMessage::REPLY_ROOT,
            ClipboardFilesMessage::STATUS_OK, root.getString());
}

void ClipboardFilesExchange::onSetRequest(RfbInputGate *input)
{
  // The body is read first and in full: a refused request must still leave
  // the connection in step.
  ClipboardFilesMessage message;
  message.readSetBody(input);

  if (!isEnabled()) {
    m_log->info(_T("Clipboard file paste refused: file transfer is disabled"));
    sendReply(ClipboardFilesMessage::REPLY_SET,
              ClipboardFilesMessage::STATUS_FAILED,
              _T("File transfer is disabled on the server"));
    return;
  }

  StringStorage expectedRoot;
  {
    AutoLock al(&m_stagingRootMutex);
    expectedRoot = m_stagingRoot;
  }
  // The only folder this client may name is the one the server handed out to
  // it. Everything else is refused before any path is looked at.
  if (expectedRoot.isEmpty() ||
      !expectedRoot.isEqualTo(message.getRoot())) {
    m_log->error(_T("Clipboard file paste refused: the staging folder \"%s\"")
                 _T(" was not handed out to this client"),
                 message.getRoot()->getString());
    sendReply(ClipboardFilesMessage::REPLY_SET,
              ClipboardFilesMessage::STATUS_FAILED,
              _T("The staging folder was not handed out to this client"));
    return;
  }

  ClipboardFilesMessage::CheckResult check = message.check();
  if (check != ClipboardFilesMessage::CHECK_OK) {
    m_log->error(_T("Clipboard file paste refused: %s"),
                 ClipboardFilesMessage::describe(check));
    sendReply(ClipboardFilesMessage::REPLY_SET,
              ClipboardFilesMessage::STATUS_FAILED,
              ClipboardFilesMessage::describe(check));
    return;
  }

  m_desktop->setNewClipFiles(message.getRoot(), message.getPaths());

  m_log->info(_T("Clipboard file paste: %d entries put on the clipboard"),
              (int)message.getPaths()->size());
  sendReply(ClipboardFilesMessage::REPLY_SET,
            ClipboardFilesMessage::STATUS_OK, _T(""));
}

void ClipboardFilesExchange::sendReply(UINT8 kind, UINT8 status,
                                       const TCHAR *text)
{
  try {
    AutoLock al(m_output);
    m_output->writeUInt32(ServerMsgDefs::CLIPBOARD_FILES_REPLY);
    ClipboardFilesMessage::writeReplyBody(m_output, kind, status, text);
    m_output->flush();
  } catch (Exception &e) {
    m_log->error(_T("Could not send a clipboard file paste reply: %s"),
                 e.getMessage());
  }
}
