FarSightVNC Viewer v1.5.0a (alpha)
==================================

VNC client for Windows based on TightVNC Viewer 2.8.90.
License: GNU GPL version 2 or later (see LICENSE.txt).
Date: 2026-11-24 - branch develop.


Pinned favorites on Home, local shortcut capture and a button to release
the keyboard to the local computer (v1.3.0a). Print Screen is local by
default and configurable. Optional Ctrl+V file paste requires FarSightVNC
support on both ends; it stays dormant with existing TightVNC servers.
Security fixes in file transfer, and a switch for it (v1.4.0a).
Browser-style tabs and the new look in every window, the password prompt
included (v1.5.0a). Everything from earlier versions is retained. See
CHANGES IN v1.5.0a below.


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


CHANGES IN v1.5.0a (tabs and the new look of the dialogs)
--------------------------------------------------------
- Tabs: several connections in one window, like a browser. The "+" next to
  the tabs opens a new tab on the start page. Click a tab to switch to it.
  The tab's "x" or the middle mouse button closes it. Ctrl+N still opens a
  new window.
- A session in a tab that is not in front stays connected but does not get
  the keyboard. If its connection drops and you stop reconnecting, the tab
  goes back to the start page in the same place.
- On the start page and in Settings: Ctrl+T opens a tab, Ctrl+W closes the
  current one, and Ctrl+Tab / Ctrl+Shift+Tab switch tabs. Inside a session
  these keys go to the remote computer like any other key.
- In listening mode, an incoming connection opens as a tab in the window
  you used last.
- The password prompt has a new design: it shows the server address and
  has a "Show password" option.
- The other windows that still looked like old TightVNC now follow the
  Light/Dark theme: reconnection, the full screen notice, file paste,
  command line help, connection options and file transfer.
- Known issue: when you open a tab that is trying to reconnect, the remote
  screen area may show what the previous tab left on screen. It is only
  visual and goes away when the session is back.

Nothing changes about how you connect to the TightVNC servers you already
have.


CHANGES IN v1.4.0a (file transfer security)
-------------------------------------------
This is a security release. It fixes problems found in a code audit, the
most serious of them in file transfer. Nothing changes about how you
connect to the TightVNC servers you already have.

- File transfer now asks once per session, the first time you use it.
  Under Settings > Input > Restrictions you choose between asking (the
  default), always allowing, or never allowing. With "never" the viewer
  does not even announce the feature to the server, and the connection is
  normal in every other way.
- The same question covers pasting files with Ctrl+V, because it uses the
  same transfer underneath. If you answer no, Ctrl+V goes through to the
  remote program as usual.
- When downloading, the name of each file the server sends is now checked.
  A name that would place the file outside the folder you chose is refused
  and logged. Before, a hostile or compromised server could write outside
  that folder, including into your Windows startup folder, with no prompt
  at all.
- Downloading over an existing file now has Skip as the default button
  instead of Overwrite, so a stray Enter cannot destroy a file of yours.
- If a download target is a folder that already exists, the download fails
  with a clear error instead of the folder being deleted. If it is a
  junction, it is refused.
- Malformed data from the server now ends the session with a message,
  instead of the viewer carrying on and reading the stream out of step.
  Just reconnect.

What this release does NOT do: it adds no encryption. The VNC protocol
used with TightVNC servers still has no encryption and no server
authentication. On an untrusted network, use a VPN or an SSH tunnel.

CHANGES IN v1.3.0a (local keyboard, favorites and optional file paste)
--------------------------------------------------------------------
- Favorites stay fixed; only recent connections scroll.
- Add key captures key-down before ordinary Windows hotkey handling. For a
  screenshot on Numpad Minus, add that exact key in Settings > Input > Local
  shortcuts; it is different from the regular minus key.
- Release keyboard in the session toolbar, full-screen toolbar and menus
  hands the keyboard to local Windows, including modifiers. Click again to
  type remotely. Held remote keys are released when switching.
- Print Screen is a local, editable entry by default. Remove it from the
  list to send it remotely. Use Release keyboard for local combinations.
- File/folder paste requires the FSVC capability advertised by the server.
  Files are staged and the remote clipboard confirmed before Ctrl+V is sent.
  A handshake timeout/cancellation requires reconnecting before retrying,
  because the protocol has no request identifier.
- Existing TightVNC servers do not offer FSVC clipboard file paste. Normal
  Ctrl+V is preserved there. Only the viewer and updater are distributed,
  without a VNC server or service.
- Per-user installer sources are in the repository; Setup still needs its
  own compilation and validation. The portable zip layout is unchanged.

CHANGES IN v1.2.1a (bug fixes and in-app updater)
-------------------------------------------------
- Install updates from inside the viewer: the update window shows the version
  and the release notes, with Install Update, Skip This Version and Remind Me
  Later. After the download is verified you choose Restart and Install Now or
  Install When I Close FarSightVNC. The updater then starts the viewer again
  and the next start tells you once that the update was applied. Nothing is
  installed without your confirmation; a failed install restores the previous
  version.
- Release notes are shown formatted (headings, bold, italic, lists, links,
  code), in the Light and Dark themes. Only https links are clickable.
- Automatic update checks: on start and every 30 minutes while the viewer
  stays open, but GitHub is asked at most once every 24 hours. A manual check
  always works.
- Local shortcuts (Settings > Input): keys you list there, such as Numpad
  Minus, are never sent to the remote computer and reach Windows and your
  local tools, for example a screenshot program. Print Screen stays local as
  before.
- Home screen: only the list of recent connections scrolls; the address
  field, Connect and the Options and Listen row stay in place.
- Fix: the remote Ctrl key no longer gets stuck (a key held while the
  keyboard hook was re-armed lost its key-up).
- Fix: update notices are no longer hidden behind other windows; in full
  screen they wait until you leave full screen.
- Fix: update checks no longer happen only once per start.
- The updater refuses repeated or conflicting command-line options and never
  restarts the viewer from an elevated process.
- Updating from v1.2.0a or older runs the old updater, so the viewer may not
  restart by itself after that one update.
- Protocol, server side, .vnc files and command-line options are unchanged.
  Tests (x64 Release): keyboard 519/0, input queue 39/0, favorites 121/0,
  updater 2384/0, keyboard end-to-end 42/42, release scripts 31/31.


CHANGES IN v1.2.0a (new interface, phase 2: themes and icons)
------------------------------------------------------------
- The Auto, Light and Dark themes of Settings > Appearance really work now.
  Auto follows the Windows app theme and changes with it, right away.
- The dark theme covers the whole window: title bar, Home screen, Settings,
  session toolbar, full screen bar, fields, lists and the window border.
- Switching the theme repaints every open viewer window at once, with no need
  to close and reopen the program.
- The icon in the top left corner of the window is now the application icon,
  in place of the placeholder square.
- Nothing else changed: protocol, server side, updater, .vnc files, command
  line options and saved settings are the same as in v1.1.0a.


CHANGES IN v1.1.0a (new interface, phase 1: layout)
---------------------------------------------------
- New window: its own title bar with the session tab, a Settings button and
  the Windows 11 Snap Layouts on the maximize button.
- New Home screen when you open the viewer (address, recent and favorite
  computers, listening mode); it replaces the old login dialog.
- New "+" button after the tab (or Ctrl+N): opens a new window on Home.
- New Settings page inside the window (Appearance, Connection, Input,
  Display, General, Updates, About); it replaces the old configuration and
  about dialogs.
- New session toolbar and a floating full screen bar. Full screen now stays
  above the taskbar and other windows.
- The remote screen always fits the window (aspect ratio kept), with no
  scroll bars. The -scale= option is ignored.
- First Lucide icon (see THIRD-PARTY NOTICES). The Appearance setting
  (Auto/Light/Dark) is saved, but the themes come in a later version.
- Includes the keyboard fixes of v1.0.1a. Protocol, server side, updater and
  saved settings are unchanged.


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
- The repository report docs/reports/continuidade-codex-2026-11-16.md records
  current build/test results separately from historical tests.
- The fake clipboard server exercises the real RFB core over in-memory pipes;
  it does not upload files or set CF_HDROP on a remote Windows machine.


NOT YET VERIFIED
----------------
- Download/install of this version over a real network and the UAC prompt
  for protected folders.
- Connection to a real TightVNC server in this version.
- Full file paste with actual upload and the remote Windows clipboard.
- Lightshot on the friend's physical ABNT2 keyboard using Numpad Minus.
- Setup compilation and install/uninstall in a disposable account or VM.
- Use on a real network (outside 127.0.0.1) and with servers other than
  TightVNC.
- Real typing on a physical keyboard and real mouse use in this version
  (the tests are automated).
- Windows 10 (the interface was tested on Windows 11), a real DPI change
  while the window is open, several monitors, and screen readers.

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
