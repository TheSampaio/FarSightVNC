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

#ifndef _THEME_H_
#define _THEME_H_

// No util/winhdr.h: Theme.cpp includes this header under a newer WINVER
// (DWMWA_USE_IMMERSIVE_DARK_MODE is hidden by the 0x0501 it forces).
#include <windows.h>

#include "ThemeTokens.h"

// The theme of the whole process: the palette the chrome is drawn with and
// the dark mode of the window frames and the native controls.
//
// Auto follows the Windows app theme; Light and Dark force one. setMode() is
// called once at start up with the stored setting and again whenever the user
// changes it; every window that owns theme-dependent GDI objects or native
// controls handles changedMessage(), which broadcast() sends.
//
// Nothing here depends on client-config-lib: the caller passes the value it
// read from the configuration.
namespace Theme {

// Same values as ViewerConfig::Theme, so the stored setting goes straight in.
enum Mode { MODE_AUTO = 0, MODE_LIGHT = 1, MODE_DARK = 2 };

// Stores the mode and re-evaluates the effective theme. When it changed, the
// process dark mode is applied and broadcast() tells the windows.
void setMode(int mode);
int mode();
// Effective theme, after Auto is resolved against the Windows setting.
bool isDark();
// Palette of the effective theme: kDarkTheme or kLightTheme.
const ThemeTokens &tokens();

// Reads the Windows app theme again. Returns true when the effective theme
// changed, which only happens in Auto (WM_SETTINGCHANGE "ImmersiveColorSet").
bool refreshSystem();

// Message sent to every window of the process when the theme changes,
// registered once (RegisterWindowMessage, so it is not a constant: compare
// with an if before the switch).
UINT changedMessage();
// Sends changedMessage() to every window of this process and redraws them.
void broadcast();

// Makes the frame of the window (border, caption of a native frame) follow
// the theme. Does nothing before Windows 10 1809.
void applyWindowDarkMode(HWND hwnd);
// Makes one native control follow the theme: the undocumented
// AllowDarkModeForWindow() plus the theme name of its class (a BUTTON needs
// the Button theme itself, since it draws its own label and ignores
// WM_CTLCOLORBTN; a combo box or a field takes the CFD variant; everything
// else takes Explorer).
void applyControlTheme(HWND control);
// applyControlTheme() on every direct child of parent.
void applyControlThemeTree(HWND parent);

// Draws a check box or a radio button itself (NM_CUSTOMDRAW of the parent,
// nmcustomdraw being the NMCUSTOMDRAW of the notification): the glyph comes
// from the theme and the label is drawn with the color of the palette, which
// a themed BUTTON would not use (it draws its own label in a tone meant for
// a light background, so an unchecked one stays dark gray on the dark
// background). Returns false in the light theme, for any other control or
// draw stage and when the theme API is unavailable: the caller then answers
// CDRF_DODEFAULT and the control draws itself as usual.
bool customDrawButton(HWND button, void *nmcustomdraw);

}

// Short name for the call sites that only need the colors.
inline const ThemeTokens &currentTheme() { return Theme::tokens(); }

#endif
