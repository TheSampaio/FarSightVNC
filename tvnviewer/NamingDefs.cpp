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
// Modified for FarSightVNC on 2026-11-03: user-visible product and window title
// name is FarSightVNC (registry, log and window class names unchanged).
// Modified for FarSightVNC on 2026-11-11: window class of the session view hosted by the shell window.
//

#include "NamingDefs.h"

const TCHAR ProductNames::PRODUCT_NAME[] = _T("TightVNC");
const TCHAR ProductNames::VIEWER_PRODUCT_NAME[] = _T("FarSightVNC");

const TCHAR LogNames::VIEWER_LOG_FILE_STUB_NAME[] = _T("tvnviewer");
const TCHAR LogNames::LOG_DIR_NAME[] = _T("TightVNC");

const TCHAR RegistryPaths::VIEWER_PATH[] = _T("Software\\TightVNC\\Viewer");

const TCHAR ApplicationNames::WINDOW_CLASS_NAME[] = 
  _T("TvnApplicationClass");

const TCHAR WindowNames::TVN_WINDOW_CLASS_NAME[] = _T("TvnWindowClass");
const TCHAR WindowNames::TVN_WINDOW_TITLE_NAME[] = _T("FarSightVNC");
const TCHAR WindowNames::TVN_SUB_WINDOW_TITLE_NAME[] = _T("Viewer");
const TCHAR WindowNames::FSV_SESSION_VIEW_CLASS_NAME[] = _T("FsvSessionView");

