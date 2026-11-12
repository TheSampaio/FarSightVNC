FarSightVNC v1.1.0a (alpha) Pre-release

New interface release of FarSightVNC, a Windows VNC viewer based on TightVNC Viewer 2.8.90 (GPLv2-or-later). It replaces v1.0.1a and includes its keyboard fixes; the updater, the protocol, authentication, encodings, `.vnc` files, command-line options and saved settings are unchanged.

## Improvements

- **New window.** The viewer has its own title bar with the session tab, a Settings button and the Windows 11 Snap Layouts on the maximize button.
- **Home screen.** Opening the viewer shows the address field, recent and favorite computers and the listening mode, in place of the old login dialog.
- **New connection "+" button** right after the tab (also Ctrl+N and "New connection" in the menu): opens a new window on Home. It is the first Lucide icon of the redesign.
- **Settings page** inside the window: Appearance, Connection, Input, Display, General, Updates and About. It replaces the old configuration and about dialogs. The Appearance choice (Auto/Light/Dark) is saved; the themes themselves come in a later version.
- **Session toolbar and full screen bar** redrawn; full screen now stays above the taskbar and other windows.
- **The remote screen always fits the window** with its aspect ratio kept and no scroll bars. The `-scale=` option is ignored.

Updating: Help > Check for Updates (tray menu, window menu or About dialog), or
download the zip and extract it over the old folder.

## Tests

- Unit tests: keyboard 480/0, input queue 39/0, favorites 88/0, updater 2103/0 (x64, Release).
- Against a fake server on 127.0.0.1 with the GDI and GPU renderers: connection smoke test 4/4, display and pointer test 194/0, keyboard end-to-end 39/0.
- Not verified: Windows 10, a real DPI change, several monitors, a physical keyboard and mouse, screen readers, and a real TightVNC Server in this version.

## Downloads

The zip `FarSightVNC-v1.1.0a.zip` (flat: `fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt`, `README.txt`) is attached together with the signed `farsightvnc-update.manifest` and `farsightvnc-update.manifest.sig`, which must stay attached for the updater to install this version automatically.

**Source:** branch `feature/ui-layout`. TightVNC copyright © GlavSoft LLC is retained. Icons by Lucide (ISC License, see `vendor/lucide/LICENSE`). The full list of changes is in `MODIFICATIONS.md`.
