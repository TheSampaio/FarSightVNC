FarSightVNC Viewer v0.3.0a (alpha)
==================================

VNC client for Windows based on TightVNC Viewer 2.8.90.
License: GNU GPL version 2 or later (see LICENSE.txt).
Date: 2026-11-09 - branch feature/updater.


HOW TO USE
----------
- x64\tvnviewer.exe   -> 64-bit Windows (recommended)
- Win32\tvnviewer.exe -> 32-bit Windows

No installation needed: open tvnviewer.exe, enter the server address
(e.g. 192.168.1.20::5900) and connect. The executable keeps its original
name, tvnviewer.exe.

Command line (examples):
  tvnviewer.exe 192.168.1.20::5900
  tvnviewer.exe -renderer=gpu 192.168.1.20::5900


COMPATIBILITY
-------------
Works with existing TightVNC servers with no change on the server side. The
protocol, authentication, encodings, .vnc files and the settings stored in
the registry are unchanged.


UPDATES (new in v0.3.0a)
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
- v0.2.1a and older do not have the updater: update them manually once.


CHANGES IN v0.3.0a
------------------
- New: optional, portable auto-updater (see UPDATES above). New file
  FarSightUpdater.exe next to tvnviewer.exe, in x64\ and Win32\ (keep it in
  the same folder as tvnviewer.exe).
- The VNC protocol, the server side and the saved settings are unchanged.


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
- Automated tests: updater 2080/0 (x64 and Win32, Release and Debug),
  keyboard 227/0, input queue 39/0, favorites 88/0.
- Installation of the signed release over a copy of v0.2.1a, with the old
  viewer open (the updater waits), user files kept, a tampered update
  rejected and a downgrade refused (automatic end-to-end test, no network).


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

Full history: MODIFICATIONS.md in the repository.
