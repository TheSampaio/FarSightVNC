FarSightVNC v1.2.1a (alpha)

This is the sixth numbered release of FarSightVNC, a Windows VNC viewer based on TightVNC Viewer 2.8.90 (GPLv2-or-later).


> ⚠️ **Alpha build: bug fixes and a complete in-app updater.**
> - This release replaces **v1.2.0a** and keeps every feature and fix of that version.
> - Update with **Check for Updates** (Settings > Updates), or download the zip and extract it over the old folder. Your settings, favorites and `.vnc` files are not affected.
> - This is the first release that carries a signed manifest, so it is the first one the updater can install by itself.

## What's new

- **Install updates from inside FarSightVNC.**
  - The update window shows the version and the release notes, with **Install Update**, **Skip This Version** and **Remind Me Later**.
  - After the download is verified you choose **Restart and Install Now** or **Install When I Close FarSightVNC**.
  - After a restart the updater starts FarSightVNC again, and the next start tells you once that the update was applied.
  - Nothing is installed without your confirmation, and a failed install restores the previous version.
- **Release notes are shown formatted.** Headings, bold and italic, lists, links, code and separators are rendered, in the Light and Dark themes. Only `https` links are clickable and no HTML is run.
- **Automatic update checks.** FarSightVNC checks on start and then every 30 minutes while it stays open, but asks GitHub at most once every 24 hours. Several open windows or processes share one request. A manual check always works.
- **Local shortcuts (Settings > Input).** Keys you list there, such as Numpad Minus, are never sent to the remote computer and reach Windows and your local tools, for example a screenshot program. Print Screen stays local as before.
- **Home screen:** only the list of recent connections scrolls. The address field, **Connect** and the Options and Listen row stay in place.

## Fixes

- **Remote Ctrl no longer gets stuck.**
  - Cause: a key already held when the keyboard hook was re-armed (for example on window activation) lost its key-up, and the next focus change pressed Ctrl on the remote computer with nothing to release it.
  - Win+R, Ctrl+C/Ctrl+V, Alt+Tab, Win+E and Ctrl+Shift+Esc were checked in automated sequences, including interrupted, focus-change and long runs.
- **Screenshot shortcuts other than Print Screen** (Lightshot on Numpad Minus, for example) now work while the viewer has focus, once you add the key under Local shortcuts.
- **Update notices** are no longer hidden behind other windows: the window is raised without taking focus and the taskbar button flashes. In full screen it waits until you leave full screen.
- **Update checks** no longer happen only once per start, and every automatic result is written to the viewer log.

## Security

- No change to the VNC protocol, authentication or server-side components.
- The signed-manifest mechanism is unchanged and was reviewed independently:
  - ECDSA P-256 signature over the manifest, checked against a public key built into the program.
  - Size and SHA-256 check of the package and of every file before anything is installed.
  - The version must match the release tag and be newer than the running one, so downgrades are refused.
  - The private signing key is not part of the repository or of any release file.
- The updater now refuses repeated or conflicting command-line options and never restarts the viewer from an elevated process.
- Release notes are parsed by a small built-in parser; links are limited to `https` and the text is never interpreted as RTF or HTML.

## Tests

- Automated, x64 Release: keyboard 519/0, input queue 39/0, favorites 121/0, updater 2384/0, keyboard end-to-end 42/42, release scripts 31/31.
- The key sequences were simulated with the project test harness and a fake RFB server; the real Windows keyboard hook and a real server were not part of the run.
- Not verified in this version: Lightshot itself, a real TightVNC Server, Windows 10, a real DPI change, several monitors, screen readers, the Light/Dark look of the new update window on screen, and an installation from a published release.

## Downloads

| File | Content |
|---|---|
| `FarSightVNC-v1.2.1a.zip` | Windows 64-bit viewer, updater and documentation |
| `farsightvnc-update.manifest` | Signed update manifest used by the updater |
| `farsightvnc-update.manifest.sig` | Manifest signature used by the updater |

Keep the manifest and its signature attached to the release. Without them the updater cannot install this version automatically.

## Known limitations

- Updating **from v1.2.0a or older** runs the old updater, so FarSightVNC may not restart by itself after that one update. Automatic restart works from this version on.
- A Ctrl or Shift held *before* you activate the viewer reaches the remote computer with its first key repeat, so a Ctrl+click right after Alt+Tab into the viewer can arrive as a plain click.
- A local shortcut matches the bare key. Combinations such as Ctrl+Numpad Minus are not kept local, and Numpad digits depend on Num Lock.
- If the remote TightVNC Server runs with lower privileges than the focused remote window (an administrator console, UAC), the server may fail to release a key; that behavior belongs to the server and is not changed here.
- The updater uses a folder you can write to; when the folder needs administrator rights, Windows asks for approval once at install time.
- **SmartScreen:** the executables are not Authenticode-signed, so Windows may show a warning on first launch.
- **Shared settings:** viewer settings are still shared with TightVNC Viewer (`HKCU\Software\TightVNC\Viewer\`); the new local shortcut list is stored there as `LocalKeys`.

## Documentation

- **Portuguese:** `LEIAME.txt`
- **English:** `README.txt`
- **Full release notes:** `docs/updater/release-notes-v1.2.1a.md`
- **Technical report:** `docs/reports/v1.2.1a-relatorio.md`

**Source:** based on TightVNC Viewer 2.8.90 (GPLv2-or-later). TightVNC copyright © GlavSoft LLC is retained. Icons by Lucide (ISC License, see `vendor/lucide/LICENSE`). The full list of modifications is in `MODIFICATIONS.md`.
