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
// The pass/fail counter shared by every test file of viewer-security-test, so
// that each area of the security tests lives in its own translation unit and
// no two of them have to edit the same file.
//

#ifndef __SECURITYTESTHARNESS_H__
#define __SECURITYTESTHARNESS_H__

#include <stdio.h>

// Header only and dependency-light on purpose: one standard header, no project
// header, so a test file includes exactly the code it is about to test.
namespace SecurityTest
{
  // The counters of one test runner. Each runner is called as
  // run<Area>Tests(int *passed, int *failed) by SecurityTestMain.cpp and wraps
  // those two pointers in a Counters object:
  //
  //   void runSomethingTests(int *passed, int *failed)
  //   {
  //     SecurityTest::Counters counters(passed, failed);
  //     counters.check(somethingIsTrue(), "something: the rule holds");
  //   }
  class Counters
  {
  public:
    Counters(int *passed, int *failed) : m_passed(passed), m_failed(failed) {}

    // Records one result. A failure prints the message, so the output of the
    // run names every rule that broke.
    void check(bool ok, const char *what)
    {
      if (ok) {
        (*m_passed)++;
      } else {
        (*m_failed)++;
        printf("FAIL: %s\n", what);
      }
    }

    // Records a failure without a condition, for a test that cannot even reach
    // its checks (a missing precondition, an exception that must not happen).
    void fail(const char *what) { check(false, what); }

  private:
    int *m_passed;
    int *m_failed;
  };
}

#endif // __SECURITYTESTHARNESS_H__
