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

#ifndef _PASTE_KEYSTROKE_H_
#define _PASTE_KEYSTROKE_H_

class RfbKeySym;

// The Ctrl+V the viewer sends to the remote application once the remote
// clipboard holds the pasted files. It is a function of its own, and not part
// of DesktopWindow, so that the sequence it produces can be checked against a
// fake server.
class PasteKeystroke
{
public:
  // Sends Ctrl down, V down, V up, Ctrl up through 'keySym'. The pair is
  // complete whether or not the user still holds the real Ctrl: its key-up is
  // ignored by RfbKeySym when it did not press the key, so nothing stays
  // pressed on the remote machine.
  static void send(RfbKeySym *keySym);
};

#endif // _PASTE_KEYSTROKE_H_
