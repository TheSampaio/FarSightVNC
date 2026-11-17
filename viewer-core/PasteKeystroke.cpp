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

#include "PasteKeystroke.h"

#include "rfb/RfbKeySym.h"
#include "util/CommonHeader.h"

void PasteKeystroke::send(RfbKeySym *keySym)
{
  unsigned int scan = MapVirtualKey('V', MAPVK_VK_TO_VSC) & 0xff;
  keySym->sendModifier(VK_CONTROL, true);
  keySym->processKeyEvent('V', scan << 16);
  keySym->processKeyEvent('V', (scan << 16) | 0x80000000);
  keySym->sendModifier(VK_CONTROL, false);
}
