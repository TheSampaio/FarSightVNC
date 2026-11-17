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

#include "ClipboardFilesCapability.h"

#include "rfb/MsgDefs.h"
#include "rfb/VendorDefs.h"
#include "thread/AutoLock.h"

ClipboardFilesCapability::PasteContext::PasteContext()
: capabilityEnabled(false),
  fileTransferEnabled(false),
  viewOnly(false),
  connected(false),
  clipboardEnabled(false),
  clipboardHasFiles(false),
  pasteInProgress(false),
  fileTransferBusy(false)
{
}

bool ClipboardFilesCapability::shouldInterceptPaste(const PasteContext &context)
{
  // Nothing to paste as files: an ordinary Ctrl+V.
  if (!context.clipboardHasFiles || !context.clipboardEnabled) {
    return false;
  }
  // A peer that does not offer the extension (every stock TightVNC server)
  // or that cannot stage the files must never see a swallowed keystroke.
  if (!context.capabilityEnabled || !context.fileTransferEnabled) {
    return false;
  }
  if (context.viewOnly || !context.connected) {
    return false;
  }
  // One paste at a time, and never while the file transfer dialog is using
  // the same file transfer core.
  if (context.pasteInProgress || context.fileTransferBusy) {
    return false;
  }
  return true;
}

ClipboardFilesCapability::ClipboardFilesCapability(Logger *logger)
: m_logWriter(logger),
  m_output(0),
  m_isSupported(false),
  m_listener(0)
{
}

ClipboardFilesCapability::~ClipboardFilesCapability()
{
}

void ClipboardFilesCapability::addCapabilities(CapabilitiesManager *capabilities)
{
  capabilities->addClientMsgCapability(ClientMsgDefs::CLIPBOARD_FILES_BEGIN,
                                       VendorDefs::FARSIGHT,
                                       ClipboardFilesDefs::CLIPBOARD_FILES_BEGIN_SIG,
                                       _T("Clipboard file paste start"));
  capabilities->addClientMsgCapability(ClientMsgDefs::CLIPBOARD_FILES_SET,
                                       VendorDefs::FARSIGHT,
                                       ClipboardFilesDefs::CLIPBOARD_FILES_SET_SIG,
                                       _T("Clipboard file paste file list"));
  capabilities->addServerMsgCapability(this,
                                       ServerMsgDefs::CLIPBOARD_FILES_REPLY,
                                       VendorDefs::FARSIGHT,
                                       ClipboardFilesDefs::CLIPBOARD_FILES_REPLY_SIG,
                                       _T("Clipboard file paste reply"));
}

void ClipboardFilesCapability::setOutput(RfbOutputGate *output)
{
  m_output = output;
}

void ClipboardFilesCapability::updateSupport(const std::vector<UINT32> *enabledClientMsgCodes)
{
  bool hasBegin = false;
  bool hasSet = false;
  for (size_t i = 0; i < enabledClientMsgCodes->size(); i++) {
    UINT32 code = (*enabledClientMsgCodes)[i];
    if (code == ClientMsgDefs::CLIPBOARD_FILES_BEGIN) {
      hasBegin = true;
    } else if (code == ClientMsgDefs::CLIPBOARD_FILES_SET) {
      hasSet = true;
    }
  }
  m_isSupported = hasBegin && hasSet;
  m_logWriter.info(_T("Clipboard file paste is %s by the server"),
                   m_isSupported ? _T("supported") : _T("not supported"));
}

void ClipboardFilesCapability::sendBegin()
{
  if (!m_isSupported || m_output == 0) {
    return;
  }
  AutoLock al(m_output);
  m_output->writeUInt32(ClientMsgDefs::CLIPBOARD_FILES_BEGIN);
  m_output->flush();
}

void ClipboardFilesCapability::sendSet(const ClipboardFilesMessage *message)
{
  if (!m_isSupported || m_output == 0) {
    return;
  }
  AutoLock al(m_output);
  m_output->writeUInt32(ClientMsgDefs::CLIPBOARD_FILES_SET);
  message->writeSetBody(m_output);
  m_output->flush();
}

void ClipboardFilesCapability::onServerMessage(UINT32 code,
                                               DataInputStream *input)
{
  if (code != ServerMsgDefs::CLIPBOARD_FILES_REPLY) {
    return;
  }

  UINT8 kind = 0;
  UINT8 status = 0;
  StringStorage text;
  ClipboardFilesMessage::readReplyBody(input, &kind, &status, &text);

  bool ok = status == ClipboardFilesMessage::STATUS_OK;
  if (m_listener == 0) {
    m_logWriter.detail(_T("Clipboard file paste reply %d dropped: no listener"),
                       (int)kind);
    return;
  }

  switch (kind) {
  case ClipboardFilesMessage::REPLY_ROOT:
    m_listener->onClipboardFilesRoot(ok, &text);
    break;
  case ClipboardFilesMessage::REPLY_SET:
    m_listener->onClipboardFilesSet(ok, &text);
    break;
  default:
    m_logWriter.error(_T("Unknown clipboard file paste reply kind %d"),
                      (int)kind);
    break;
  }
}
