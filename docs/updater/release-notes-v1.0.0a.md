FarSightVNC v1.0.0a (alpha) Pre-release

This is the third numbered release of FarSightVNC, a Windows VNC viewer based on TightVNC Viewer 2.8.90 (GPLv2-or-later).

> ⚠️ **Alpha build with a breaking change.**
> - The program is now `bin\x64\fsvnc-viewer.exe` (64-bit) or `bin\x86\fsvnc-viewer.exe` (32-bit). Update any shortcuts or scripts that pointed to `FarSightVNC.exe` or `tvnviewer.exe`.
> - This is the first release with a built-in **automatic updater**.
> - Install it manually once: extract the zip into a **new** folder. Your settings and `.vnc` files are not affected.

## What's new

- **Built-in updater:**
  - **Check for Updates...** is available in the tray menu, the window system menu and the About dialog.
  - The update dialog shows the release notes and offers **Download and Install**, **Remind Me Later** and **Skip This Version**.
  - An automatic daily check is optional (a checkbox in the update dialog). Pre-release versions are opt-in for stable users and enabled by default on alpha/beta builds.
  - The download runs in the background. The update is installed only after every FarSightVNC window of that folder is closed, so active sessions are never interrupted.
  - It is fully portable: no installer, service or scheduled task. Administrator permission is requested only when the folder is protected (for example under `Program Files`).
- **New folder layout:** `bin\x64\` and `bin\x86\`, each with `fsvnc-viewer.exe` and `fsvnc-updater.exe`, plus `LICENSE.txt`, `README.txt` and `LEIAME.txt`.
- **Application name:** Windows (for example Task Manager) now shows just "FarSightVNC". The About dialog still says "Based on TightVNC Viewer 2.8.90".
- **Modernized code base:**
  - The whole solution is built as C++20.
  - Third-party code lives in `vendor/`, and build output goes to `build/bin` and `build/obj`.
  - An experimental CMake build with precompiled headers is available.
- **Version:** the executable properties and the About dialog show 1.0.0a.

## Fixes

- **Memory safety (hardening against malicious or broken servers):**
  - A huge desktop name sent by the server is truncated to 64 KiB instead of exhausting memory. The connection continues.
  - Fixed out-of-range reads when decoding ZRLE with compact pixels (CPIXEL).
  - Fixed an infinite loop in `FrameBuffer::setColor` when the server reports a 0×0 framebuffer or no pixel format.

## Security

- **Signed updates:** every update is verified before anything is installed.
  - Each release carries a manifest signed with ECDSA P-256 / SHA-256, checked with Windows CNG against a public key compiled into the application.
  - The manifest pins the package and every installed file by size and SHA-256.
  - Tampered, corrupted, incomplete, older (downgrade) and mismatched releases are refused.
- **Network:** HTTPS only, with system certificate validation always on and HTTPS→HTTP redirects refused. The only request goes to GitHub, carrying just the version in the User-Agent. No telemetry.
- **Installation:**
  - Files are replaced in a transaction with backup and automatic rollback, and an interrupted update is repaired the next time FarSightVNC closes.
  - Files that are not part of the release are never touched.
- **Corporate environments:** administrators can disable all update checks with the DWORD `HKLM\SOFTWARE\Policies\FarSightVNC\Viewer\Updater\DisableUpdateCheck = 1`. An unreachable GitHub or a proxy never prevents the viewer from starting or working.

## Interface

- New update dialog in the native Windows style, with no new runtime dependencies.
- Automatic checks open the dialog without taking focus, only when a new version exists, and never show error messages. Errors are shown only for a manual check.
- The VNC protocol, authentication, encodings, `.vnc` files, command-line options and saved settings are unchanged.

## Diagnostics

- **Viewer log:** set the log level to **9** in Options > Configuration. The viewer writes to `%APPDATA%\TightVNC\tvnviewer.log` (the file name is unchanged).
- **Updater log:** `%LOCALAPPDATA%\FarSightVNC\Updates\<id>\updater.log`. The result of the last installation is in `last-result.txt` in the same folder.
- **Updater settings:** `HKCU\Software\FarSightVNC\Viewer\Updater`.
- **Tests:**
  - `updater-test` runs 2097 deterministic checks of the updater.
  - `tools/release/Test-EndToEndInstall.ps1` installs this signed release over an older version (x64 and x86), including tamper and downgrade cases.

## Downloads

| File | Content | SHA-256 |
|---|---|---|
| `FarSightVNC-v1.0.0a.zip` | Windows 10/11 64-bit (`bin\x64`) and 32-bit (`bin\x86`) | `518ff897e1d0f03c2d8afcece566a84612be4893787391bdac7fcfa451999cef` |
| `farsightvnc-update.manifest` | Signed update manifest (used by the updater) | `95ea8e7e1d7525882f5a05decf30fab7b0d461024a29e01347ce7e115632e44c` |
| `farsightvnc-update.manifest.sig` | Manifest signature (used by the updater) | `d7ccf2bc4940f6c8b436882eee1e0ee19211e60d09cd5cddbd61c1aa1c400e89` |

The manifest and the signature must stay attached to the release. Without them the updater will not install this version automatically.

## Known limitations

- **Manual install required once:** v0.1.1a and older have no updater.
- **Not yet validated by hand:**
  - a real download of a published release by the updater;
  - the administrator (UAC) prompt in a protected folder;
  - every state of the update dialog.
- **Not tested:**
  - a corporate proxy;
  - Windows 7;
  - a full disk during an update;
  - DPI above 96;
  - multiple monitors;
  - a real GPU device loss.
- **SmartScreen:** the executables are not Authenticode-signed, so Windows may warn on first launch.
- **Shared settings:** settings are shared with TightVNC Viewer (`HKCU\Software\TightVNC\Viewer`).

**Source:** commit `5087629` (branch `feature/updater`). TightVNC copyright © GlavSoft LLC is retained. The full list of changes is in `MODIFICATIONS.md`.
