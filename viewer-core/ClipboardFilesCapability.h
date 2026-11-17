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

#ifndef _CLIPBOARD_FILES_CAPABILITY_H_
#define _CLIPBOARD_FILES_CAPABILITY_H_

#include "log-writer/LogWriter.h"
#include "network/RfbOutputGate.h"
#include "rfb/ClipboardFilesMessage.h"
#include "thread/LocalMutex.h"

#include "CapabilitiesManager.h"
#include "ServerMessageListener.h"

#include <vector>

// Answers of the server to the clipboard file paste requests.
class ClipboardFilesListener
{
public:
  // Answer of CLIPBOARD_FILES_BEGIN. On success 'text' is the staging folder
  // the files must be uploaded into, otherwise it describes the failure.
  virtual void onClipboardFilesRoot(bool ok, const StringStorage *text) = 0;
  // Answer of CLIPBOARD_FILES_SET. On success the remote clipboard holds the
  // file list and the paste may be released to the remote application.
  virtual void onClipboardFilesSet(bool ok, const StringStorage *text) = 0;

protected:
  ~ClipboardFilesListener() {}
};

// Client side of the clipboard file paste extension: the capabilities, the
// two client-to-server messages and the reply of the server.
//
// Nothing is ever sent unless the server advertised the capability, so a
// stock TightVNC server sees exactly the traffic it saw before.
class ClipboardFilesCapability : public ServerMessageListener
{
public:
  // Everything the decision "does this Ctrl+V become a file paste?" depends
  // on. It is a plain structure with a pure function over it so that the
  // decision can be tested without a server, a clipboard or a window.
  struct PasteContext
  {
    PasteContext();

    // The server advertised the clipboard file paste capability.
    bool capabilityEnabled;
    // The server advertised the file transfer, which stages the files.
    bool fileTransferEnabled;
    // The local session is view-only.
    bool viewOnly;
    // The session is established.
    bool connected;
    // Clipboard sharing is switched on in the local session options.
    bool clipboardEnabled;
    // The local clipboard holds a file list (CF_HDROP).
    bool clipboardHasFiles;
    // A clipboard file paste of this session is still running.
    bool pasteInProgress;
    // The file transfer dialog is using the shared file transfer core.
    bool fileTransferBusy;
  };

  // True when the Ctrl+V key-down must be swallowed and turned into a file
  // paste. False means "let the keystroke through unchanged", which is what
  // happens with a stock TightVNC server, with file transfer disabled, in
  // view-only mode and whenever the clipboard holds no files.
  static bool shouldInterceptPaste(const PasteContext &context);

  ClipboardFilesCapability(Logger *logger = 0);
  virtual ~ClipboardFilesCapability();

  // Must be called before RemoteViewerCore::start().
  void addCapabilities(CapabilitiesManager *capabilitiesManager);

  // Must be called from CoreEventsAdapter::onConnected().
  void setOutput(RfbOutputGate *output);

  // Takes the negotiated capabilities in both directions. A paste needs
  // BEGIN, SET and REPLY, all matched with the FarSight vendor signature.
  void updateSupport(const std::vector<UINT32> *enabledClientMsgCodes,
                     const std::vector<UINT32> *enabledServerMsgCodes);
  bool isSupported() const { return m_isSupported; }

  void setListener(ClipboardFilesListener *listener);

  // Asks the server for a staging folder. The answer arrives through
  // ClipboardFilesListener::onClipboardFilesRoot.
  void sendBegin();
  // Tells the server which files were staged. The answer arrives through
  // ClipboardFilesListener::onClipboardFilesSet.
  void sendSet(const ClipboardFilesMessage *message);

  // Overrides ServerMessageListener::onServerMessage().
  virtual void onServerMessage(UINT32 code, DataInputStream *input);

private:
  LogWriter m_logWriter;

  RfbOutputGate *m_output;
  bool m_isSupported;
  LocalMutex m_listenerMutex;
  ClipboardFilesListener *m_listener;
};

#endif // _CLIPBOARD_FILES_CAPABILITY_H_
