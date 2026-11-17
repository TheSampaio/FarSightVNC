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
// Deterministic tests of the limits applied to the RFB stream: counts and
// sizes that arrive from the server must have an explicit ceiling before
// they size an allocation or drive a loop, and a pixel format must be
// rejected when it would make the viewer divide by zero or shift past the
// width of a pixel.
//
// Covers the audit findings A-04, A-07, A-08, A-10, B-04, B-07 and C-04.
//
// The runner is wired into SecurityTestMain.cpp already; the checks
// themselves are added by the phase that fixes these findings.
//

#include "viewer-security-test/SecurityTestHarness.h"

void runRfbLimitsTests(int *passed, int *failed)
{
  SecurityTest::Counters counters(passed, failed);
  (void)counters;
}
