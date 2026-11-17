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
// Deterministic tests of the file transfer path rules: the remote peer
// chooses every file name in a listing, so a name must never be able to
// place the written file outside the folder the operator picked, and a
// download must never remove or overwrite a file on its own.
//
// Covers the audit findings D-01, D-03, D-06 and D-07.
//
// The runner is wired into SecurityTestMain.cpp already; the checks
// themselves are added by the phase that fixes these findings.
//

#include "viewer-security-test/SecurityTestHarness.h"

void runFileTransferPathTests(int *passed, int *failed)
{
  SecurityTest::Counters counters(passed, failed);
  (void)counters;
}
