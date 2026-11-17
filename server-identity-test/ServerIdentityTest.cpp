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

// Deterministic test that the FarSightVNC server has an identity of its own and
// therefore cannot take over, reconfigure or disturb the TightVNC server that is
// installed on the same machine.
//
// Why a test and not a code review: every single one of these names is a
// machine-wide resource. If any of them still said "tvnserver", "TightVNC",
// "tvncontrol", "TVN_" or "TVN.", then installing or starting FarSightVNC would
// reach into the TightVNC installation - the service name would collide (and
// TvnService::reinstall() removes before it installs), the registry key would be
// shared, the Event Log source would be overwritten, the named pipes and the
// global mutexes would be answered by the wrong process, and
// RegisterWindowMessage() codes (which are system-wide per string) would be
// delivered across the two products. A silent regression here costs the owner
// his working remote access, so it is asserted constant by constant.
//
// This test only reads constants and constructs one ServerConfig in memory. It
// never touches the registry, never touches a service, never binds a socket and
// never starts a process.

#include "tvnserver-app/NamingDefs.h"
#include "server-config-lib/ServerConfig.h"

#include <stdio.h>
#include <string.h>

namespace {

int g_passed = 0;
int g_failed = 0;
int g_pending = 0;

void check(bool ok, const char *what)
{
  if (ok) {
    g_passed++;
  } else {
    g_failed++;
    printf("FAIL: %s\n", what);
  }
}

// Reports without failing the run: the requirement is agreed, the rename it
// depends on is a separate task. See testHookLibraryNamePending().
void pending(bool ok, const char *what)
{
  if (ok) {
    g_passed++;
  } else {
    g_pending++;
    printf("PENDING (expected to fail until the project rename lands): %s\n", what);
  }
}

bool equals(const TCHAR *value, const TCHAR *expected)
{
  return value != 0 && _tcscmp(value, expected) == 0;
}

bool startsWith(const TCHAR *value, const TCHAR *prefix)
{
  return value != 0 && _tcsncmp(value, prefix, _tcslen(prefix)) == 0;
}

bool containsNoCase(const TCHAR *value, const TCHAR *needle)
{
  StringStorage haystack(value);
  StringStorage pattern(needle);
  haystack.toLowerCase();
  pattern.toLowerCase();
  return _tcsstr(haystack.getString(), pattern.getString()) != 0;
}

bool contains(const TCHAR *value, const TCHAR *needle)
{
  return _tcsstr(value, needle) != 0;
}

// The whole point of the test: no identity constant may carry a TightVNC name.
// "TightVNC", "tvnserver" and "tvncontrol" are compared without case, because
// the collision is on resources that ignore case (service names, registry keys,
// Event Log sources) or that the owner reads by eye. "TVN_" and "TVN." are
// compared as written, since they are the prefixes of the pipe name and of the
// RegisterWindowMessage strings.
void checkClean(const TCHAR *value, const char *name)
{
  char message[256];

  if (value == 0 || value[0] == _T('\0')) {
    g_failed++;
    printf("FAIL: %s is empty\n", name);
    return;
  }

  static const TCHAR *const forbiddenNoCase[] = {
    _T("TightVNC"), _T("tvnserver"), _T("tvncontrol")
  };
  for (size_t i = 0; i < sizeof(forbiddenNoCase) / sizeof(forbiddenNoCase[0]); i++) {
    _snprintf_s(message, sizeof(message), _TRUNCATE,
                "%s must not contain a TightVNC name", name);
    check(!containsNoCase(value, forbiddenNoCase[i]), message);
  }

  static const TCHAR *const forbiddenExact[] = { _T("TVN_"), _T("TVN.") };
  for (size_t i = 0; i < sizeof(forbiddenExact) / sizeof(forbiddenExact[0]); i++) {
    _snprintf_s(message, sizeof(message), _TRUNCATE,
                "%s must not carry a TVN prefix", name);
    check(!contains(value, forbiddenExact[i]), message);
  }
}

// ---------------------------------------------------------------------------
// Change 1: the server's own identity (tvnserver-app/NamingDefs.cpp).
// ---------------------------------------------------------------------------

// Every constant is listed, so that a name added to NamingDefs.cpp later and
// forgotten here is visible as a gap in this list rather than as a silent hole.
void testNoTightVncNameSurvives()
{
  checkClean(ProductNames::PRODUCT_NAME, "ProductNames::PRODUCT_NAME");
  checkClean(ProductNames::SERVER_PRODUCT_NAME, "ProductNames::SERVER_PRODUCT_NAME");

  checkClean(RegistryPaths::SERVER_PATH, "RegistryPaths::SERVER_PATH");
  checkClean(RegistryPaths::SERVER_REVERSE_CONN_HISTORY_PATH,
             "RegistryPaths::SERVER_REVERSE_CONN_HISTORY_PATH");

  checkClean(WindowNames::WINDOW_CLASS_NAME, "WindowNames::WINDOW_CLASS_NAME");

  checkClean(LogNames::SERVER_LOG_FILE_STUB_NAME, "LogNames::SERVER_LOG_FILE_STUB_NAME");
  checkClean(LogNames::LOG_DIR_NAME, "LogNames::LOG_DIR_NAME");
  checkClean(LogNames::LOG_PIPE_PUBLIC_NAME, "LogNames::LOG_PIPE_PUBLIC_NAME");
  checkClean(LogNames::WIN_EVENT_PROVIDER_NAME, "LogNames::WIN_EVENT_PROVIDER_NAME");

  checkClean(ClipboardNames::CLIPBOARD_WIN_CLASS_NAME,
             "ClipboardNames::CLIPBOARD_WIN_CLASS_NAME");

  checkClean(ServiceNames::TVNCONTROL_START_REGISTRY_ENTRY_NAME,
             "ServiceNames::TVNCONTROL_START_REGISTRY_ENTRY_NAME");
  checkClean(ServiceNames::SERVICE_NAME, "ServiceNames::SERVICE_NAME");
  checkClean(ServiceNames::SERVICE_NAME_TO_DISPLAY, "ServiceNames::SERVICE_NAME_TO_DISPLAY");

  checkClean(ServerApplicationNames::SERVER_INSTANCE_MUTEX_NAME,
             "ServerApplicationNames::SERVER_INSTANCE_MUTEX_NAME");
  checkClean(ServerApplicationNames::CONTROL_APP_INSTANCE_MUTEX_NAME,
             "ServerApplicationNames::CONTROL_APP_INSTANCE_MUTEX_NAME");
  checkClean(ServerApplicationNames::FOR_SERVICE_CONTROL_APP_PIPE_NAME,
             "ServerApplicationNames::FOR_SERVICE_CONTROL_APP_PIPE_NAME");
  checkClean(ServerApplicationNames::FOR_APP_CONTROL_APP_SERVICE_PIPE_NAME,
             "ServerApplicationNames::FOR_APP_CONTROL_APP_SERVICE_PIPE_NAME");

  checkClean(HookDefinitions::HOOK_LOADER_WINDOW_CLASS,
             "HookDefinitions::HOOK_LOADER_WINDOW_CLASS");
  checkClean(HookDefinitions::HOOK_TARGET_WIN_CLASS_NAME,
             "HookDefinitions::HOOK_TARGET_WIN_CLASS_NAME");
  checkClean(HookDefinitions::HOOK_LOADER_NAME, "HookDefinitions::HOOK_LOADER_NAME");

  checkClean(DefaultNames::DEFAULT_COMPUTER_NAME, "DefaultNames::DEFAULT_COMPUTER_NAME");
}

// A clean name is not enough: the name has to be the one that was agreed, or two
// FarSight builds could disagree with each other and the owner would chase a
// mismatch between the service, the registry key and the configuration tool.
void testAgreedNames()
{
  check(equals(ServiceNames::SERVICE_NAME, _T("fsvncserver")),
        "service name is fsvncserver");
  check(equals(ServiceNames::SERVICE_NAME_TO_DISPLAY, _T("FarSightVNC Server")),
        "service display name is FarSightVNC Server");
  check(equals(ServiceNames::TVNCONTROL_START_REGISTRY_ENTRY_NAME, _T("fsvnccontrol")),
        "Run value name is fsvnccontrol");

  check(equals(RegistryPaths::SERVER_PATH, _T("Software\\FarSightVNC\\Server")),
        "server registry key is Software\\FarSightVNC\\Server");
  check(startsWith(RegistryPaths::SERVER_REVERSE_CONN_HISTORY_PATH,
                   _T("Software\\FarSightVNC\\Control")),
        "reverse connection history lives under Software\\FarSightVNC\\Control");

  check(equals(LogNames::WIN_EVENT_PROVIDER_NAME, _T("fsvncserver")),
        "Event Log source is fsvncserver");
  check(equals(LogNames::SERVER_LOG_FILE_STUB_NAME, _T("fsvncserver")),
        "server log file stub is fsvncserver");
  check(equals(LogNames::LOG_DIR_NAME, _T("FarSightVNC")),
        "log directory is FarSightVNC");

  check(equals(ServerApplicationNames::FOR_SERVICE_CONTROL_APP_PIPE_NAME,
               _T("FarSightVNC_Service_Control")),
        "service control pipe is FarSightVNC_Service_Control");
  check(equals(ServerApplicationNames::FOR_APP_CONTROL_APP_SERVICE_PIPE_NAME,
               _T("FarSightVNC_Application_Control")),
        "application control pipe is FarSightVNC_Application_Control");
  check(equals(ServerApplicationNames::SERVER_INSTANCE_MUTEX_NAME,
               _T("fsvncserverApplication")),
        "server instance mutex is fsvncserverApplication");
  check(equals(ServerApplicationNames::CONTROL_APP_INSTANCE_MUTEX_NAME,
               _T("fsvncserverServiceControlSlave")),
        "control app mutex is fsvncserverServiceControlSlave");

  check(equals(ProductNames::PRODUCT_NAME, _T("FarSightVNC")),
        "product name is FarSightVNC");
  check(equals(ProductNames::SERVER_PRODUCT_NAME, _T("FarSightVNC Server")),
        "server product name is FarSightVNC Server");
  check(equals(DefaultNames::DEFAULT_COMPUTER_NAME, _T("FarSightVNC Server")),
        "default desktop name is FarSightVNC Server");

  check(equals(HookDefinitions::HOOK_LOADER_NAME, _T("fsvnchookldr.exe")),
        "hook loader executable is fsvnchookldr.exe");
}

// RegisterWindowMessage() hands out one code per string for the whole session,
// so a shared string means TightVNC's hooks and FarSight's hooks talk on the
// same channel. The registered string cannot be read back from the code, so the
// code is compared against the code of the FarSight string. Only the FarSight
// strings are registered here: registering the TVN. ones to prove inequality
// would add them to the session's global atom table, which the test has no
// business doing.
void testHookMessageCodes()
{
  check(HookDefinitions::LOADER_CLOSE_CODE != 0, "loader close code was registered");
  check(HookDefinitions::SPEC_IPC_CODE != 0, "hook IPC code was registered");
  check(HookDefinitions::LOADER_CLOSE_CODE != HookDefinitions::SPEC_IPC_CODE,
        "the two hook message codes differ");
  check(HookDefinitions::LOADER_CLOSE_CODE ==
          RegisterWindowMessage(_T("FSVC.HOOK.LOADER.CLOSE.CODE")),
        "loader close code is FSVC.HOOK.LOADER.CLOSE.CODE");
  check(HookDefinitions::SPEC_IPC_CODE ==
          RegisterWindowMessage(_T("FSVC.HOOK.MESSAGE.CODE")),
        "hook IPC code is FSVC.HOOK.MESSAGE.CODE");
}

// Deliberate exception, asserted so that a later cleanup cannot "fix" it by
// accident: this is the class name of the dfmirage mirror driver's own message
// window, not a FarSightVNC name. Renaming it would stop the driver from being
// found.
void testMirrorDriverClassNameIsUnchanged()
{
  check(equals(NamingDefs::MIRROR_DRIVER_MESSAGE_WINDOW_CLASS_NAME,
               _T("MIRROR_DRIVER_MESSAGE_WINDOW_CLASS_NAME")),
        "the mirror driver message window class name is left as the driver expects");
}

// ---------------------------------------------------------------------------
// Change 2: own defaults (server-config-lib/ServerConfig.cpp).
// ---------------------------------------------------------------------------

// A second bind on 5900 fails with WSAEADDRINUSE and
// TvnServer::restartMainRfbServer() only logs it, so the server would come up
// with no listener at all and look healthy while being unreachable. The port is
// asserted both ways: not the TightVNC one, and the one that was agreed.
void testDefaultPortsAndSwitches()
{
  ServerConfig config;

  check(config.getRfbPort() != 5900, "the default RFB port is not TightVNC's 5900");
  check(config.getRfbPort() == 5920, "the default RFB port is 5920");

  check(config.getHttpPort() != 5800, "the default HTTP port is not TightVNC's 5800");
  check(config.getHttpPort() == 5820, "the default HTTP port is 5820");

  check(!config.isAcceptingHttpConnections(),
        "the HTTP server is off by default");
  check(!config.getMirrorIsAllowed(),
        "the mirror driver is off by default (dfmirage is machine-wide)");
}

// ---------------------------------------------------------------------------
// Pending requirement: desktop/HookInstaller.cpp.
// ---------------------------------------------------------------------------

// HookInstaller::LIBRARY_NAME is private and its DynamicLibrary member loads the
// DLL in the constructor, so the constant cannot be read from a test without
// loading screenhooks64.dll. The requirement is checked on the source text
// instead, and reported as PENDING rather than as a failure, because renaming
// screenhooks64.dll is a separate task: the DLL project, its output name and the
// shipping layout all move together. When that task lands, this check turns
// green on its own; turn pending() into check() then.
//
// The scan is read-only and the path comes from the build system
// (FSV_HOOKINSTALLER_SOURCE), so the test still works from any directory.
void testHookLibraryNamePending()
{
#ifdef FSV_HOOKINSTALLER_SOURCE
  FILE *file = 0;
  if (fopen_s(&file, FSV_HOOKINSTALLER_SOURCE, "rb") != 0 || file == 0) {
    g_failed++;
    printf("FAIL: cannot read %s\n", FSV_HOOKINSTALLER_SOURCE);
    return;
  }

  char text[8192];
  size_t read = fread(text, 1, sizeof(text) - 1, file);
  fclose(file);
  text[read] = '\0';

  pending(strstr(text, "screenhooks64.dll") == 0,
          "desktop/HookInstaller.cpp must not load screenhooks64.dll");
  pending(strstr(text, "screenhooks32.dll") == 0,
          "desktop/HookInstaller.cpp must not load screenhooks32.dll");
#else
  g_failed++;
  printf("FAIL: FSV_HOOKINSTALLER_SOURCE was not defined by the build\n");
#endif
}

} // namespace

int main()
{
  testNoTightVncNameSurvives();
  testAgreedNames();
  testHookMessageCodes();
  testMirrorDriverClassNameIsUnchanged();
  testDefaultPortsAndSwitches();
  testHookLibraryNamePending();

  printf("server-identity-test: %d passed, %d failed, %d pending\n",
         g_passed, g_failed, g_pending);
  return g_failed == 0 ? 0 : 1;
}
