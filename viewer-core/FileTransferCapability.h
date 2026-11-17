// Copyright (C) 2012 GlavSoft LLC.
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
// Modified for FarSightVNC on 2026-11-17: client-side file transfer policy (ask, allow, never).
//

#ifndef _FILE_TRANSFER_CAPABILITY_H_
#define _FILE_TRANSFER_CAPABILITY_H_

#include "ft-client-lib/FileTransferRequestSender.h"
#include "ft-client-lib/FileTransferReplyBuffer.h"
#include "ft-client-lib/FileTransferMessageProcessor.h"
#include "ft-client-lib/FileTransferCore.h"

#include "log-writer/LogWriter.h"

#include "ServerMessageListener.h"
#include "CapabilitiesManager.h"

class FileTransferCapability : public ServerMessageListener
{
public:
  FileTransferCapability(Logger *logger = 0);
  virtual ~FileTransferCapability();

  //
  // Client-side switch for file transfer (audit finding D-08): until now the
  // server alone decided whether the viewer offered file transfer.
  //
  // POLICY_ASK    The capability is advertised, but file transfer is not used
  //               until the operator confirms it once in the session.
  // POLICY_ALLOW  File transfer works whenever the server supports it.
  // POLICY_NEVER  The capability is not advertised at all, which is how an
  //               optional RFB feature is switched off: a TightVNC server
  //               handles a client that does not ask for file transfer, and
  //               file transfer is simply absent (isEnabled() is false).
  //
  // Must be set before addCapabilities().
  //
  enum Policy {
    POLICY_ASK,
    POLICY_ALLOW,
    POLICY_NEVER
  };
  void setPolicy(Policy policy);
  Policy getPolicy() const;

  //
  // This method return true, if server support File Transfer and the policy
  // does not switch it off.
  //
  virtual bool isEnabled();

  //
  // The answer of the operator for this session (POLICY_ASK only). It lives
  // as long as this object, which is one session: a reconnection asks again.
  // These methods are for the window thread.
  //
  // True when file transfer can be offered: enabled and not declined.
  bool isAvailable();
  // True when file transfer may be used right now without asking anything.
  bool isPermitted();
  // True when the operator must still be asked (POLICY_ASK, no answer yet).
  bool needsConsent();
  // Stores the answer. The question is asked at most once per session, so a
  // server can never make the viewer ask again.
  void setConsent(bool granted);

  //
  // This method must called from event onConnected() in CoreEventsAdapter.
  //
  void setOutput(RfbOutputGate *output);

  //
  // Overrides MessageListener::onRequest().
  //
  virtual void onServerMessage(UINT32 code, DataInputStream *input);

  virtual FileTransferCore *getCore();
  virtual void setInterface(FileTransferInterface *ftInterface);

  //
  // This method must be called before call RemoteViewerCore::start(),
  // otherwise FT will no worked.
  //
  virtual void addCapabilities(CapabilitiesManager *capabilitiesManager);

protected:
  LogWriter m_logWriter;

  FileTransferRequestSender m_ftRequestSender;
  FileTransferReplyBuffer m_ftReplyBuffer;
  FileTransferMessageProcessor m_ftMessageProcessor;

  FileTransferCore m_ftCore;

  enum Consent {
    CONSENT_UNDECIDED,
    CONSENT_GRANTED,
    CONSENT_DENIED
  };

  Policy m_policy;
  Consent m_consent;
};

#endif
