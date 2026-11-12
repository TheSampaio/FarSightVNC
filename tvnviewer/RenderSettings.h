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
// Modified for FarSightVNC on 2026-11-02: optional GPU presentation with safe GDI fallback.
// Modified for FarSightVNC on 2026-11-11: the Settings page reads and writes the stored renderer.
//

#ifndef _RENDER_SETTINGS_H_
#define _RENDER_SETTINGS_H_

#include <windows.h>
#include <tchar.h>

// Process-wide choice of the remote desktop renderer.
//
//   gdi   - always use the GDI path (the original TightVNC behavior).
//   auto  - use the Direct3D 11 / Direct2D path and fall back to GDI when it
//           is unavailable or fails.
//   gpu   - same as "auto" (GDI is still the automatic fallback); the name
//           only states the intention in diagnostics.
//
// Sources, from the strongest to the weakest:
//   1. the "-renderer=gdi|auto|gpu" command line option (ViewerCmdLine);
//   2. the FARSIGHTVNC_RENDERER environment variable;
//   3. the string value "Renderer" of
//      HKEY_CURRENT_USER\Software\TightVNC\Viewer\Settings;
//   4. the built-in default (RenderSettings::DEFAULT_MODE).
//
// The Settings page writes the stored value (store()); it applies to the
// remote desktops created afterwards unless the command line or the
// environment chose the renderer of this run (isOverridden()).
class RenderSettings
{
public:
  enum Mode { GDI, AUTO, GPU };

  // Built-in default; see docs/reports/fase-3-relatorio.md for the reason.
  static const Mode DEFAULT_MODE = GDI;

  // Parses "gdi", "auto" or "gpu" (case insensitive). Returns false and
  // leaves *mode untouched for any other text.
  static bool parse(const TCHAR *text, Mode *mode)
  {
    if (text == 0) {
      return false;
    }
    if (_tcsicmp(text, _T("gdi")) == 0) {
      *mode = GDI;
    } else if (_tcsicmp(text, _T("auto")) == 0) {
      *mode = AUTO;
    } else if (_tcsicmp(text, _T("gpu")) == 0 || _tcsicmp(text, _T("d3d")) == 0) {
      *mode = GPU;
    } else {
      return false;
    }
    return true;
  }

  // Command line override (call before any window is created).
  static void setFromCommandLine(Mode mode)
  {
    instance().mode = mode;
    instance().resolved = true;
    instance().overridden = true;
  }

  // The renderer of this run comes from the command line or the
  // environment, not from the stored value.
  static bool isOverridden()
  {
    getMode();
    return instance().overridden;
  }

  // The stored value (HKCU ...\Settings\Renderer), or false when there is
  // none or it is invalid.
  static bool readStored(Mode *mode)
  {
    HKEY settings = 0;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_QUERY_VALUE, &settings) != ERROR_SUCCESS) {
      return false;
    }
    TCHAR buffer[32];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LONG result = RegQueryValueEx(settings, _T("Renderer"), 0, &type,
                                  reinterpret_cast<BYTE *>(buffer), &size);
    RegCloseKey(settings);
    // RegQueryValueEx does not guarantee a terminating null. Reject
    // malformed or truncated strings before handing them to parse().
    return result == ERROR_SUCCESS && type == REG_SZ &&
           size >= sizeof(TCHAR) && size <= sizeof(buffer) &&
           size % sizeof(TCHAR) == 0 && buffer[size / sizeof(TCHAR) - 1] == 0 &&
           parse(buffer, mode);
  }

  // Writes the stored value; without an override it becomes the renderer of
  // the next remote desktops of this run too.
  static bool store(Mode mode)
  {
    const TCHAR *name = mode == GDI ? _T("gdi") : (mode == AUTO ? _T("auto") : _T("gpu"));
    HKEY settings = 0;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, 0, 0, KEY_SET_VALUE, 0,
                       &settings, 0) != ERROR_SUCCESS) {
      return false;
    }
    LONG result = RegSetValueEx(settings, _T("Renderer"), 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(name),
                                static_cast<DWORD>((_tcslen(name) + 1) * sizeof(TCHAR)));
    RegCloseKey(settings);
    if (result != ERROR_SUCCESS) {
      return false;
    }
    if (!isOverridden()) {
      instance().mode = mode;
    }
    return true;
  }

  static Mode getMode()
  {
    Data &d = instance();
    if (!d.resolved) {
      d.mode = DEFAULT_MODE;
      Mode fromSource;
      TCHAR buffer[32];
      DWORD len = GetEnvironmentVariable(_T("FARSIGHTVNC_RENDERER"), buffer,
                                         sizeof(buffer) / sizeof(buffer[0]));
      if (len > 0 && len < sizeof(buffer) / sizeof(buffer[0]) && parse(buffer, &fromSource)) {
        d.mode = fromSource;
        d.overridden = true;
      } else if (readStored(&fromSource)) {
        d.mode = fromSource;
      }
      d.resolved = true;
    }
    return d.mode;
  }

  static bool isGpuAllowed() { return getMode() != GDI; }

private:
  struct Data
  {
    Data() : mode(DEFAULT_MODE), resolved(false), overridden(false) {}
    Mode mode;
    bool resolved;
    bool overridden;
  };

  static constexpr const TCHAR *SETTINGS_KEY = _T("Software\\TightVNC\\Viewer\\Settings");

  static Data &instance()
  {
    static Data data;
    return data;
  }
};

#endif
