FarSightVNC Viewer v1.0.1a (alpha)
==================================

VNC client for Windows based on TightVNC Viewer 2.8.90.
License: GNU GPL version 2 or later (see LICENSE.txt).
Date: 2026-11-11 - branch fix/keyboard-ctrl-shortcuts.


Keyboard fix release (v1.0.1a): Ctrl and Alt no longer get stuck and Ctrl+C /
Ctrl+V work again. See CHANGES IN v1.0.1a below.


*** BREAKING CHANGE in v1.0.0a (still applies) ***
The program is now fsvncviewer.exe, at the root of the folder. It is
64-bit only (Windows 10/11 64-bit): the 32-bit version is no longer provided.
Update shortcuts and scripts that pointed to FarSightVNC.exe or
tvnviewer.exe. Settings, .vnc files and command-line options are unchanged.


HOW TO USE
----------
Requires 64-bit Windows 10 or 11.

No installation needed: open fsvncviewer.exe, enter the server address
(e.g. 192.168.1.20::5900) and connect.

Folder layout (flat; keep the files together, the updater relies on it):
  fsvncviewer.exe, fsvncupdater.exe
  LEIAME.txt, LICENSE.txt, README.txt

Command line (examples):
  fsvncviewer.exe 192.168.1.20::5900
  fsvncviewer.exe -renderer=gpu 192.168.1.20::5900


COMPATIBILITY
-------------
Works with existing TightVNC servers with no change on the server side. The
protocol, authentication, encodings, .vnc files and the settings stored in
the registry are unchanged.


UPDATES (new in v1.0.0a)
------------------------
Using the viewer as a portable program still works without the updater: you
can ignore it and keep the files where they are.

- Check for Updates... is in the tray menu, in the window system menu (click
  the icon at the left of the title bar) and in the About dialog.
- The automatic check (once a day, at most) is optional: it is a check box in
  the update dialog. Pre-release versions (alpha/beta) are an opt-in there too.
- When an update exists, the dialog shows the release notes and offers:
  Download and Install, Remind Me Later or Skip This Version.
- The update is installed only after ALL FarSightVNC windows are closed.
  Sessions in progress are never interrupted. Your .vnc files and other files
  in the folder are not touched.
- Administrator permission is requested only if the folder is protected (for
  example under C:\Program Files).
- Every download is verified with a digital signature (ECDSA P-256) before it
  is installed. An update that fails the check is discarded. If the
  installation fails, the previous version is restored.
- To disable the update check for all users, an administrator can set the
  DWORD DisableUpdateCheck = 1 in
  HKLM\SOFTWARE\Policies\FarSightVNC\Viewer\Updater
- Manual update (always possible): download the zip from
  https://github.com/TheSampaio/FarSightVNC/releases and extract it over the
  old folder.
- Privacy: the only request is to GitHub, and the only information sent is the
  program version in the User-Agent. There is no telemetry.
- v0.2.1a and older do not have the updater: update them manually once. Because
  v1.0.0a changes the file names and the folder layout, extract it into a NEW
  folder (do not extract it over the old x64\ and Win32\ folders, which can then
  be deleted).
- Where the update is installed: always the folder of the running
  fsvncviewer.exe. If that folder contains LICENSE.txt (the extracted
  package), all five files are updated. If it does not (for example a lone
  fsvncviewer.exe moved to a folder shared with other programs), only
  fsvncviewer.exe and fsvncupdater.exe are replaced or added; no document
  (README.txt, ...) is ever written there, so a README.txt of your own stays
  as it is.


CHANGES IN v1.0.1a (keyboard fixes)
-----------------------------------
- Fixed: Ctrl, Alt and Win pressed in another window and released in the
  viewer stayed "stuck" (on the local Windows and on the server), and Ctrl+C /
  Ctrl+V (for example on an ABNT2 keyboard) were sent as Ctrl+Alt+C. A key is
  now released on the same path that pressed it.
- Fixed: the Print Screen key (also Alt+Print Screen) now stays on the local
  machine, so screenshots and the Windows Snipping Tool work while the viewer
  has focus. It is no longer sent to the server.
- Nothing else changed: protocol, server side, updater and saved settings are
  the same as v1.0.0a.


CHANGES IN v1.0.0a (first public release with the updater)
-----------------------------------------------------------
- BREAKING: the executable is now fsvncviewer.exe (it was tvnviewer.exe), at
  the root of the folder. Only 64-bit Windows 10/11 is supported: the 32-bit
  version (Win32\) is no longer provided. Update shortcuts and scripts. The
  command-line options, .vnc files and settings stored in the registry are
  unchanged.
- New: optional, portable auto-updater (see UPDATES above). New file
  fsvncupdater.exe next to fsvncviewer.exe (keep it in the same folder).
- The package is flat: fsvncviewer.exe, fsvncupdater.exe, LEIAME.txt,
  LICENSE.txt and README.txt, with no sub-folders.
- The VNC protocol, the server side and the saved settings are unchanged.
- (v0.3.0a was an internal build that was never published.)


CHANGES IN v0.2.1a
------------------
- The application name shown by Windows (e.g. in Task Manager) is now just
  "FarSightVNC", without "(based on TightVNC Viewer)". The About dialog still
  credits TightVNC 2.8.90, as the license requires.


CHANGES IN v0.2.0a (since v0.1.1a)
----------------------------------
Security fixes (protection against servers that send invalid or malicious
data):
- Huge desktop name at connection start: now truncated to 64 KiB.
  Previously, a 4 GiB length crashed the viewer.
- ZRLE decoder: every pixel write is checked against the buffer bounds
  (a latent flaw that the current viewer could not reach).
- 0x0 screen sent by the server: the viewer froze at 100% CPU (a flaw
  inherited from TightVNC). Fixed.

Technical base (no visible change for users):
- All code compiles as C++20.
- Third-party libraries moved to vendor\; build output in build\bin and
  build\obj.
- Experimental CMake build (the official build is still Visual Studio).


VERIFIED IN THIS VERSION
------------------------
- Automated tests (x64, Release): keyboard 480/0, input queue 39/0,
  favorites 88/0, updater 2103/0. The keyboard tests cover Ctrl+C/V/X/Z/A with
  both Ctrl keys, releasing keys on the path that pressed them, and Print
  Screen.
- (Verified in v1.0.0a; the updater code did not change in v1.0.1a)
  Installation of the signed release over a copy of the v0.2.1a x64
  executable placed in the new flat layout: with the old viewer open (the
  updater waits), user files kept, a tampered update rejected and a downgrade
  refused. Also the case of a lone executable in a shared folder (only the two
  .exe files are written; a README.txt of your own stays untouched). Automatic
  end-to-end test, no network.


NOT YET VERIFIED
----------------
- Download of an update from GitHub over a real network (the first release
  with the updater has no older version that can update to it) and the UAC
  prompt for protected folders.
- Connection to a real TightVNC Server in this version (the connection code
  is unchanged since v0.2.1a, which was verified against TightVNC Server
  2.8.85 on 127.0.0.1).
- Use on a real network (outside 127.0.0.1) and with servers other than
  TightVNC.
- Real typing on a physical keyboard in this version (keyboard tests are
  automated).

This is an alpha release: try it on one machine first before wider use.

THIRD-PARTY NOTICES
-------------------
Icons: Lucide (https://lucide.dev), lucide-static 0.468.0, used under the ISC
License, which includes portions from Feather (MIT). Notice of the Lucide
LICENSE file, kept as is (also in Settings > About):

ISC License

Copyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as part of Feather (MIT). All other copyright (c) for Lucide are held by Lucide Contributors 2022.

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

Full history: MODIFICATIONS.md in the repository.
