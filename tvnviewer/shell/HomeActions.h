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

#ifndef _HOME_ACTIONS_H_
#define _HOME_ACTIONS_H_

#include "util/winhdr.h"

class ConnectionConfig;
class StringStorage;

// What the Home page asks of the application (TvnViewer).
class HomeActions
{
public:
  virtual ~HomeActions() {}

  // Connects to host with config in the shell window shell, whose Home tab
  // becomes the session. Like the former login dialog, the host goes to the
  // top of the history and config is saved for it. The session starts
  // later (posted): the Home page that asks for it is destroyed then.
  virtual void connectFromHome(HWND shell, const StringStorage *host,
                               const ConnectionConfig *config) = 0;

  // Listening mode (reverse connections) and its tray icon.
  virtual bool isListening() const = 0;
  virtual void toggleListening() = 0;

  // Opens a new shell window on the Home page (posted).
  virtual void openHome() = 0;
  // Opens the configuration dialog of the viewer (posted).
  virtual void openConfiguration() = 0;
};

#endif
