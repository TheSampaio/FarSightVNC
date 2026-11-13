FarSightVNC v1.2.0a (alpha) Pre-release

Second part of the new interface of FarSightVNC, a Windows VNC viewer based on TightVNC Viewer 2.8.90 (GPLv2-or-later). It replaces v1.1.0a and keeps everything that came with it; the updater, the protocol, authentication, encodings, `.vnc` files, command-line options and saved settings are unchanged.

## Improvements

- **The themes work.** Auto, Light and Dark in Settings > Appearance really change the window now. Auto follows the Windows app theme and reacts to it right away; Light and Dark force one.
- **The dark theme covers the whole window:** title bar, Home screen, Settings page, session toolbar, full screen bar, fields, lists, check boxes, radio buttons and the window border.
- **Switching the theme repaints every open viewer window at once,** with no need to close and reopen the program.
- **The real application icon** is in the top left corner of the title bar, in place of the placeholder square, drawn at the size the window DPI asks for.

## Tests

- Build: x64 Release, no new warnings.
- Checked on screen with the viewer running: Home and Settings in both themes, Light to Dark switched from inside the program, Auto following Windows, and the labels of the check boxes and radio buttons readable in Appearance, Connection and General. The light theme is unchanged.
- Unit tests: keyboard 480/0, input queue 39/0, favorites 88/0, updater 2103/0 (x64, Release).
- Not verified in this version: Windows 10, a real DPI change, several monitors, screen readers, a real TightVNC Server, and the dark theme in the legacy dialogs that still use Windows controls outside the Settings page (file transfer, for example).

## Downloads

The zip `FarSightVNC-v1.2.0a.zip` (flat: `fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt`, `README.txt`) is attached together with the signed `farsightvnc-update.manifest` and `farsightvnc-update.manifest.sig`, which must stay attached for the updater to install this version automatically.
