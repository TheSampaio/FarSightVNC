FarSightVNC v1.0.1a (alpha) Pre-release

Keyboard fix release of FarSightVNC, a Windows VNC viewer based on TightVNC Viewer 2.8.90 (GPLv2-or-later). It replaces v1.0.0a; the updater, the protocol, authentication, encodings, `.vnc` files, command-line options and saved settings are unchanged.

## Bug fixes

- **Ctrl and Alt no longer get stuck.** A Ctrl, Alt or Win key pressed in another window (or before the viewer was started) and released in the viewer stayed down on the local Windows, and every time the viewer regained focus it was pressed again on the server. A key is now released on the same path that pressed it.
- **Ctrl+C and Ctrl+V work again.** On keyboards such as ABNT2 these shortcuts were sent to the server as Ctrl+Alt+C and Ctrl+Alt+V while a modifier was stuck; Ctrl+C/V/X/Z/A are now sent correctly with either Ctrl key.
- **Print Screen stays on your machine.** The key (also Alt+Print Screen) is no longer captured or sent to the server, so Windows screenshots and the Snipping Tool work while the viewer has focus.

Updating: Help > Check for Updates (tray menu, window menu or About dialog), or
download the zip and extract it over the old folder.

## Tests

- Keyboard unit tests: 480 checks, 0 failures (new cases for the hook routing, Ctrl chords and Print Screen).
- Input queue 39/0, favorites 88/0, updater 2103/0 (x64, Release).
- Not validated by hand on a physical keyboard against a real TightVNC Server in this version.

## Downloads

The zip `FarSightVNC-v1.0.1a.zip` (flat: `fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt`, `README.txt`) is attached together with the signed `farsightvnc-update.manifest` and `farsightvnc-update.manifest.sig`, which must stay attached for the updater to install this version automatically.

**Source:** branch `fix/keyboard-ctrl-shortcuts`. TightVNC copyright © GlavSoft LLC is retained. The full list of changes is in `MODIFICATIONS.md`.
