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

#ifndef __CLIPBOARDFILESEXCHANGE_H__
#define __CLIPBOARDFILESEXCHANGE_H__

#include "RfbCodeRegistrator.h"
#include "RfbDispatcherListener.h"
#include "desktop/Desktop.h"
#include "log-writer/LogWriter.h"
#include "network/RfbOutputGate.h"
#include "rfb/ClipboardFilesMessage.h"
#include "thread/LocalMutex.h"

// Per-client handler of the clipboard file paste messages (see the wire
// format next to the message codes in rfb/MsgDefs.h). One direction only:
// the viewer stages the copied files on this machine through the ordinary
// file transfer and then asks for a CF_HDROP of them on the clipboard of the
// interactive user.
//
// The capability is only advertised when the paste can really happen (file
// transfer enabled, TIGHT in use, not view-only), so a viewer that would get
// no service never sends the messages and its Ctrl+V stays a plain
// keystroke.
class ClipboardFilesExchange : public RfbDispatcherListener
{
public:
  // 'enabled' must be the same condition the file transfer is created with:
  // the paste stages its files through the file transfer, so it cannot work
  // without it.
  ClipboardFilesExchange(RfbCodeRegistrator *codeRegtor, Desktop *desktop,
                         RfbOutputGate *output, bool enabled, LogWriter *log);
  virtual ~ClipboardFilesExchange();

protected:
  // Inherited from RfbDispatcherListener.
  virtual void onRequest(UINT32 reqCode, RfbInputGate *input);

private:
  // Creates a staging folder for one paste and answers with its path.
  void onBeginRequest();
  // Reads the path list, checks it and, if it is sound, puts a CF_HDROP of
  // it on the clipboard of the interactive user. The whole body is always
  // read, also when the request is refused, so that the connection stays in
  // step.
  void onSetRequest(RfbInputGate *input);

  void sendReply(UINT8 kind, UINT8 status, const TCHAR *text);

  // The paste is only served while the file transfer is allowed; the
  // configuration can be reloaded during a session, so it is checked again
  // on every request and not only when this object is created.
  bool isEnabled() const;

  Desktop *m_desktop;
  RfbOutputGate *m_output;
  bool m_enabled;

  // The staging folder handed out to this client by the last
  // CLIPBOARD_FILES_BEGIN. A CLIPBOARD_FILES_SET is only served for exactly
  // this folder: the client cannot name a folder of its own and cannot reuse
  // the folder of another client.
  StringStorage m_stagingRoot;
  mutable LocalMutex m_stagingRootMutex;

  LogWriter *m_log;
};

#endif // __CLIPBOARDFILESEXCHANGE_H__
