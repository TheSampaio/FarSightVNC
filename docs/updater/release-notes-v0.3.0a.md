## Security fixes
- None in this release. The new updater verifies every download with a digital
  signature before anything is installed (see Improvements).

## Bug fixes
- None in this release.

## Improvements
- Optional, portable auto-updater. FarSightVNC can now check this GitHub
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
  - New file `FarSightUpdater.exe` next to `tvnviewer.exe` (x64 and Win32).
- The VNC protocol, authentication, encodings, `.vnc` files, saved settings and
  the server side are unchanged. The viewer works exactly as before if the
  updater is disabled or GitHub is unreachable.

Updating: v0.2.1a and older have no updater, so install this version manually
once: download `FarSightVNC-v0.3.0a.zip` and extract it over the old folder (your
`.vnc` files are not touched). From v0.3.0a on, use **Check for Updates...**.

Assets: `FarSightVNC-v0.3.0a.zip`, `farsightvnc-update.manifest`,
`farsightvnc-update.manifest.sig`. This is an alpha pre-release.
