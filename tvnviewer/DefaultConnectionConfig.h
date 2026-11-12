// Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
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

#ifndef _DEFAULT_CONNECTION_CONFIG_H_
#define _DEFAULT_CONNECTION_CONFIG_H_

#include "client-config-lib/ConnectionConfig.h"
#include "client-config-lib/ConnectionConfigSM.h"
#include "NamingDefs.h"

// Options of new connections (Settings, Connection and Input sections). They
// are stored like the options of a host, in the History\.default key (the
// listening mode has History\.listen); a host saved in the history keeps the
// options it was last used with. Missing values keep the built-in defaults.
class DefaultConnectionConfig
{
public:
  static void load(ConnectionConfig *config)
  {
    ConnectionConfigSM ccsm(RegistryPaths::VIEWER_PATH, ENTRY_NAME);
    config->loadFromStorage(&ccsm);
  }

  static void save(ConnectionConfig *config)
  {
    ConnectionConfigSM ccsm(RegistryPaths::VIEWER_PATH, ENTRY_NAME);
    config->saveToStorage(&ccsm);
  }

private:
  static constexpr const TCHAR *ENTRY_NAME = _T(".default");
};

#endif
