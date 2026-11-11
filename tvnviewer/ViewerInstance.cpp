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
// Modified for FarSightVNC on 2026-11-03: a reconnection attempt shows its window while connecting.
// Modified for FarSightVNC on 2026-11-11: the session window is a child of the given frame (SessionHost).
//

#include "ViewerInstance.h"

#include "viewer-core/RemoteViewerCore.h"
#include "viewer-core/FileTransferCapability.h"

ViewerInstance::ViewerInstance(WindowsApplication *application,
                               ConnectionData *condata,
                               const ConnectionConfig *conConf,
                               SessionHost *host)
: m_conConf(*conConf),
  m_condata(*condata),
  m_socket(0),
  m_viewerWnd(application,
              &m_condata,
              &m_conConf,
              host,
              ViewerConfig::getInstance()->getLogger()),
  m_vncAuthHandler(&m_condata),
  m_viewerCore(ViewerConfig::getInstance()->getLogger())
{
}

ViewerInstance::ViewerInstance(WindowsApplication *application,
                               ConnectionData *condata,
                               const ConnectionConfig *conConf,
                               SessionHost *host,
                               SocketIPv4 *socket)
: m_conConf(*conConf),
  m_condata(*condata),
  m_socket(socket),
  m_viewerWnd(application,
              &m_condata,
              &m_conConf,
              host,
              ViewerConfig::getInstance()->getLogger()),
  m_vncAuthHandler(&m_condata),
  m_viewerCore(ViewerConfig::getInstance()->getLogger())
{
}


ViewerInstance::~ViewerInstance()
{
  // Modified for FarSightVNC on 2026-11-02: let the core drain input before
  // it shuts down outbound or accepted reverse sockets.
  m_viewerCore.stop();
  m_viewerCore.waitTermination();

  if (m_socket != 0) {
    delete m_socket;
    m_socket = NULL;
  }
}

void ViewerInstance::waitViewer()
{
  m_viewerCore.waitTermination();
}

ViewerWindow *ViewerInstance::getViewerWindow()
{
  return &m_viewerWnd;
}

bool ViewerInstance::requiresReconnect() const
{
  return m_viewerWnd.requiresReconnect();
}

bool ViewerInstance::isStopped() const
{
  return m_viewerWnd.isStopped();
}

void ViewerInstance::stop()
{
  m_viewerWnd.postMessage(ViewerWindow::WM_USER_STOP);
}

void ViewerInstance::start()
{
  Logger *logger = ViewerConfig::getInstance()->getLogger();
  m_viewerWnd.setRemoteViewerCore(&m_viewerCore);


  m_viewerWnd.setFileTransfer(&m_fileTransfer);

  m_vncAuthHandler.addAuthCapability(&m_viewerCore);

  m_fileTransfer.addCapabilities(&m_viewerCore);

  if (m_socket) {
    m_viewerCore.start(m_socket,
                       &m_viewerWnd, m_conConf.getSharedFlag());
  } else {
    StringStorage strHost;
    m_condata.getReducedHost(&strHost);
    UINT16 portVal = m_condata.getPort();
    // The window of a reconnection attempt shows the "connecting" state;
    // an ordinary connection keeps it hidden until the session is established.
    if (m_condata.getReconnectAttempt() > 0) {
      m_viewerWnd.showConnecting();
    }
    m_viewerCore.start(strHost.getString(), portVal,
                       &m_viewerWnd, m_conConf.getSharedFlag());
  }
}
