## Breaking change
- The program is now `bin\x64\fsvnc-viewer.exe` (64-bit) or
  `bin\x86\fsvnc-viewer.exe` (32-bit). It was `tvnviewer.exe` in `x64\` and
  `Win32\` before. Update shortcuts and scripts that pointed to `tvnviewer.exe`.
  Settings, `.vnc` files and command-line options are unchanged.

## Security fixes
- None in this release. The new updater verifies every download with a digital
  signature before anything is installed (see Improvements).

## Bug fixes
- None in this release.

## Improvements
- First public release with the updater. FarSightVNC can now check this GitHub
  release page and install new versions in place. There is no installer, service
  or scheduled task, and it needs no administrator rights unless the folder is
  protected.
  - **Check for Updates...** is in the tray menu, the window system menu and the
    About dialog. An automatic daily check is optional (a checkbox in the update
    dialog) and pre-release versions are an opt-in.
  - The update dialog shows the release notes and offers **Download and Install**,
    **Remind Me Later** and **Skip This Version**.
  - The update is installed only after every FarSightVNC window of that folder is
    closed. Running sessions are never interrupted. If anything fails, the
    previous version is restored.
  - Downloads are verified before installation: the release carries a signed
    manifest (ECDSA P-256 / SHA-256, Windows CNG, no new runtime dependency) that
    pins the package and every file by size and SHA-256. Older versions and
    mismatched tags are refused.
  - The only network request is to GitHub, with the version in the User-Agent.
    No telemetry.
  - Administrators can disable all checks with the DWORD
    `HKLM\SOFTWARE\Policies\FarSightVNC\Viewer\Updater\DisableUpdateCheck = 1`.
  - New file `fsvnc-updater.exe` next to `fsvnc-viewer.exe` (`bin\x64` and
    `bin\x86`). Keep the folder layout as it is in the zip.
- The VNC protocol, authentication, encodings, `.vnc` files, saved settings and
  the server side are unchanged. The viewer works exactly as before if the
  updater is disabled or GitHub is unreachable.

Updating: v0.2.1a and older have no updater, and the folder layout changed, so
install this version manually once: download `FarSightVNC-v1.0.0a.zip` and
extract it into a NEW folder (your `.vnc` files and settings are not affected;
the old `x64\` and `Win32\` folders can be deleted). From v1.0.0a on, use
**Check for Updates...**. (v0.3.0a was an internal build that was never
published.)

Assets: `FarSightVNC-v1.0.0a.zip`, `farsightvnc-update.manifest`,
`farsightvnc-update.manifest.sig`. This is an alpha pre-release.
