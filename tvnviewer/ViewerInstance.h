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
// Modified for FarSightVNC on 2026-11-11: the session window is a child of the given frame (SessionHost).
//

#ifndef __VIEWER_INSTANCE_H__
#define __VIEWER_INSTANCE_H__

#include "ViewerWindow.h"
#include "network/socket/SocketIPv4.h"

#include "ViewerVncAuthHandler.h"


class ViewerInstance
{
public:
  // creates the viewer instance by using host:port from condata; its
  // session window is shown in host
  ViewerInstance(WindowsApplication *application,
                 ConnectionData *condata,
                 const ConnectionConfig *conConf,
                 SessionHost *host);

  // creates the viewer instance if we have the socket
  ViewerInstance(WindowsApplication *application,
                 ConnectionData *condata,
                 const ConnectionConfig *conConf,
                 SessionHost *host,
                 SocketIPv4 *socket);

  ViewerWindow *getViewerWindow();

  virtual ~ViewerInstance();

  bool requiresReconnect() const;
  bool isStopped() const;
  void start();
  void stop();

  // wait while viewer is not terminated
  void waitViewer();

protected:
  ConnectionData m_condata;
  ConnectionConfig m_conConf;

  ViewerWindow m_viewerWnd;
  RemoteViewerCore m_viewerCore;
  ViewerVncAuthHandler m_vncAuthHandler;
  FileTransferCapability m_fileTransfer;
  SocketIPv4 *m_socket;
};

#endif
