# FarSightVNC auto-updater — design

Status: approved for implementation; first released in v1.0.0a. Owner of this document: the updater work stream.
Public interfaces: `updater-lib/*.h` (the contract; change them only through the manager).

## Goals and non-goals

- Portable updates from the official GitHub Releases of `TheSampaio/FarSightVNC` (confirmed from `git remote`).
- Users choose to accept, postpone or skip a release, and can read the release notes before updating.
- Updates never interrupt a VNC session. Installation runs only after every viewer process of that installation has exited.
- No installer, service, scheduled task, daemon, admin requirement or new runtime dependency. Only Windows system DLLs are used (winhttp, bcrypt, version, shell32).
- No change to the RFB protocol, the VNC server or existing TightVNC settings. The updater is optional: if it is disabled or GitHub is unreachable, the viewer works exactly as before.

## Reference: PCSX2 (UX only, no code)

PCSX2 is GPLv3. None of its code is copied; FarSightVNC is GPLv2-or-later.

What we keep from it:
- The three-button dialog: Download and Install / Skip This Version / Remind Me Later.
- Skip is persisted per version.
- Background checks stay quiet on errors and do not pop up over a running session.

What we do differently, because PCSX2 lacks it:
- Authenticity and integrity verification. PCSX2 has none.
- Backup and rollback. PCSX2 replaces files without either.
- The updater waits for the application processes. PCSX2's helper does not.
- A manual check re-offers a skipped version. PCSX2 does not do this.

## Components

| Component | Kind | Responsibility |
|---|---|---|
| `updater-lib` | static lib, no viewer deps, links vendor zlib | Version, JSON, Releases selection, Crypto (CNG), signed Manifest, ZipReader, FsUtil, WinHTTP client, UpdateService (settings/policy/check/stage), Installer (journal + rollback), process scan |
| `farsight-updater` → `fsvnc-updater.exe` | Windows exe, ~same arch as viewer | wait for viewer processes, re-verify staged update, permission check + conditional UAC, install, rollback, recovery, cleanup. No network code is called. |
| `tvnviewer` (integration) → `fsvnc-viewer.exe` | existing exe | `UpdateController` (worker threads, posts results to the hidden main window), `UpdateDialog` (modeless), "Check for Updates..." entries, launching the updater at process exit |
| `updater-test` | console test exe | deterministic tests for all of the above (exit code 0 = pass) |
| `tools/release/*.ps1` | PowerShell 5.1 | key generation, manifest creation + signing + self-verification |

Why a separate updater exe: the files must be replaced after the viewer exits, the elevated part must be a separate process (UAC works per process), and keeping network and parsing in the unelevated viewer means the elevated process only handles authenticated data.

## Authenticity (security-critical decision)

- A SHA-256 published next to the zip on the same release is not proof of authenticity: anyone who can change the release can change both.
- There is no Authenticode certificate, so neither publisher signatures nor WinVerifyTrust are available.
- **Chosen:** a signed manifest using ECDSA P-256 over SHA-256, verified with Windows CNG. The public key is compiled in (`updater-lib/TrustedKeys.cpp`). The private key stays offline with the owner (`%USERPROFILE%\.farsightvnc-signing\`), never in the repository.
- This is minisign-like. It needs no new dependency and no new infrastructure, only the PowerShell signing script.
- The manifest pins the package and every installed file by size and SHA-256.
- The manifest is verified before it is parsed.
- The manifest version must equal the release tag and be strictly greater than the running version. This blocks replay and downgrade attacks: an old signed manifest cannot be attached to a new tag, and nothing older than the running version is accepted.
- Releases without a valid manifest are never installed automatically. The dialog offers the release page instead.
- Key rotation: add the new key to `TrustedKeys.cpp` and ship a release signed with the old key. Later releases are signed with the new key.
- Key loss means affected users must update manually once. Back up the private key offline.

## Release assets (per GitHub release)

- `FarSightVNC-v<ver>.zip`, layout (entries at the zip root, `/` separators):
  ```
  bin/x64/fsvnc-viewer.exe
  bin/x64/fsvnc-updater.exe
  bin/x86/fsvnc-viewer.exe
  bin/x86/fsvnc-updater.exe
  LICENSE.txt
  README.txt
  LEIAME.txt
  ```
  `bin/x86` is the 32-bit build (the MSBuild platform `Win32`). The project and directory names in the repository (`tvnviewer/`, `farsight-updater/`) and the MSBuild output folders `build\bin\<x64|Win32>\<Config>\` are unchanged; only the output file names and the distribution layout use the names above.
- `farsightvnc-update.manifest`: the grammar is in `updater-lib/Manifest.h`.
- `farsightvnc-update.manifest.sig`: a raw 64-byte P1363 signature over the exact manifest bytes.

## Version and channel rules (`Releases.h`)

- `vMAJOR.MINOR.FIX[a|b]`. Comparison is numeric, then by channel: alpha < beta < stable. Lexicographic comparison is never used.
- A release counts as a prerelease if it has an `a`/`b` suffix or GitHub flags it as a prerelease. The two are checked together; the more conservative answer wins.
- `includePrereleases` defaults to the running build's channel. An alpha/beta build sees prereleases; a stable build does not. The user can override it in the dialog.
- Selection picks the highest eligible release newer than the running version, excluding drafts.
- Automatic checks suppress any version at or below the skipped version, so a newer release resumes notifications.
- Manual checks ignore the skip.

## Check policy (`UpdateService.h`)

- An automatic check runs at most once per launch, only when the policy is due:
  - every 24 h after an up-to-date result;
  - every 6 h after a failure;
  - after a rate limit, respecting the `X-RateLimit-Reset` time.
- If an update was found but not acted on ("Remind Me Later"), the next launch checks again and offers it again.
- Requests send only the User-Agent `FarSightVNC-Updater/<ver>` and `Accept`. There is no telemetry.
- Administrator kill switch: `HKLM\SOFTWARE\Policies\FarSightVNC\Viewer\Updater\DisableUpdateCheck = 1`.
- Settings are stored in `HKCU\Software\FarSightVNC\Viewer\Updater`, alongside the existing FarSightVNC key `...\Viewer\Favorites`. The TightVNC keys are untouched.
- Network rules:
  - WinHTTP uses automatic proxy detection (WPAD/PAC), falling back to the default proxy;
  - certificate validation is always on;
  - HTTPS→HTTP redirects are refused;
  - timeouts: resolve/connect 10 s, send 15 s, receive 30 s;
  - size caps: API 2 MiB, manifest 64 KiB, package 128 MiB.

## Install target (`Installer.h`)

- **Full** layout: the viewer lives in a folder named `x64` or `x86` whose parent is named `bin` and whose grandparent contains `LICENSE.txt` (all names case-insensitive). The grandparent is the install root and the whole package (both architectures and the docs) is updated there, mapping manifest paths 1:1. All `fsvnc-viewer.exe` targets are waited for.
- **Standalone** layout: any other place, for example a lone `fsvnc-viewer.exe` moved out of the package. Only the files under `bin/<arch>/` of the manifest are installed into that folder, with that prefix stripped.
- `--arch` takes `x64` or `x86` (`buildArchitecture()`).
- There is no migration from the pre-1.0 layout (`x64\tvnviewer.exe`, `Win32\tvnviewer.exe`): v0.3.0a and older are replaced by a manual extraction once.
- Files not listed in the manifest are never touched. This covers user `.vnc` files, other tools and logs.
- v1.0.0a renamed the executables (`tvnviewer.exe` → `fsvnc-viewer.exe`); from then on the names are stable, so shortcuts and scripts keep working across updates.

## Flow

1. **Check** (viewer worker thread): `checkForUpdate`, then `PostMessage` to the TvnViewer hidden window, then `UpdateDialog`.
   - Automatic results are shown without activation (no focus stealing) and only when an update exists.
   - Automatic failures are silent; only manual checks report errors.
2. **Download and Install** (viewer worker thread): `stageUpdate` into `%LOCALAPPDATA%\FarSightVNC\Updates\<hash(root)>\pending`. The dialog shows progress and a Cancel button. Sessions keep running.
   - Before downloading, the viewer probes `canWriteDirectory(root)`. If the folder is not writable, the dialog says administrator approval will be requested at install time.
3. **Ready**: the dialog says the update will be installed after all FarSightVNC windows are closed. The user can cancel, which discards the staged update.
4. **Viewer exit** (`main.cpp`, after `TvnViewer::run()` returns), if `pending\ready` exists:
   - copy `fsvnc-updater.exe` from the viewer folder to `<state>\bin\` (if it is missing there, use the verified staged copy `pending\files\bin\<arch>\fsvnc-updater.exe`);
   - launch it detached:
     `fsvnc-updater.exe --protocol 1 --install --state "<stateDir>" --viewer "<full path of fsvnc-viewer.exe>" --arch x64 --parent-pid <pid>`
5. **fsvnc-updater** (unelevated):
   1. take the per-root mutex `Local\FarSightVNC-Updater-<hash>` (a second instance exits);
   2. run `loadStagedUpdate`, which re-verifies the signature and file hashes;
   3. refuse if the manifest version is not greater than the target's `readExecutableVersion` (downgrade check at install time);
   4. `detectInstallTarget`, then `planInstall`;
   5. `waitForProcessesToExit` on the viewer exes, matched by full image path only, so unrelated processes with the same name are ignored;
   6. if `canWriteDirectory`/`canReplaceFile` fails, relaunch itself with `ShellExecuteEx("runas")` and the same arguments plus `--elevated`, then wait for it.
      - UAC cancelled: a message offers to open the release page, and the staged update is kept. The next viewer start shows "ready" again.
   7. `applyInstall`;
   8. on success, delete `pending` (guarded `removeTree`) and write `<state>\last-result.txt`. No dialog is shown on success.
   9. on failure, show a message box with the error, "previous version restored", and an offer to open the release page.
6. **Recovery**: if the viewer starts and `<root>\.fsv-update\journal` exists, an install was interrupted. The viewer schedules `fsvnc-updater --recover` at exit, the same way as step 4.

## Install transaction (`applyInstall`)

Journal: `<root>\.fsv-update\journal`, a text file flushed after each line.

| Line | Meaning |
|---|---|
| `begin <n>` | transaction started for n files |
| `swap <i> <had-old:0/1>` | written before swap i |
| `done <i>` | written after swap i |
| `committed` | all swaps verified |

1. Preconditions, with nothing changed if any fails:
   - no pending journal;
   - free space ≥ Σsize + 1 MiB;
   - no target in use, checked with an exclusive open;
   - the target directories are writable.
2. Copy the sources to `.fsv-update\new\<rel>` and verify SHA-256 on the copy.
3. Per file:
   - move the old target to `.fsv-update\backup\<rel>` with `MoveFileExW`;
   - move `new\<rel>` to the target with `MoveFileExW` (`MOVEFILE_WRITE_THROUGH`; same volume, so it is a rename).
4. Verify every installed file (size + SHA-256). Write `committed` and delete `.fsv-update`.
5. On failure, undo in reverse order: delete each new target and move its backup back. If a restore fails, the result is `RollbackFailed` and the journal is kept for `recoverInstall`.

## Threats considered

| Threat | Mitigation |
|---|---|
| Malicious release or asset (GitHub account or CDN compromise) | ECDSA-signed manifest with a compiled-in key; per-file hashes |
| Downgrade / replay | Manifest version must equal the tag and be greater than the running version; rechecked against the installed exe at install time |
| MITM / TLS stripping | HTTPS only, system certificate validation, no HTTPS→HTTP redirect, asset URL prefix pinned |
| Zip bomb / path traversal | Sizes bounded by the signed manifest; output paths come from the validated manifest, never from zip entry names |
| Corrupt or partial download | Size + SHA-256 checks; temp file, then rename; `ready` marker written last |
| TOCTOU between verification and install | The updater copies files into the install folder's work dir and verifies the copy before swapping |
| Elevated parsing of untrusted data | The elevated process only parses the signed manifest after signature verification. It parses no zip and makes no network calls |
| Concurrent updates | Per-root named mutex; the journal refuses a second transaction |
| Unrelated process with the same name | Process matching uses the full image path |
| DLL planting next to the updater | `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` at start; `/DEPENDENTLOADFLAG:0x800`; only system DLLs are imported |

## Known limitations (accepted)

- Same-user attackers are out of scope. Anyone running as the user can already replace a portable exe in a user-writable folder. The elevated case launches the updater from `%LOCALAPPDATA%`, which is the standard "UAC is not a security boundary for same-user" caveat.
- v0.2.1a and older have no updater. v1.0.0a (the first release with it; v0.3.0a was never published) must be installed manually once, into a new folder because the layout changed.
- The `fsvnc-updater` waiting process stays alive while other viewer processes of the same installation are still open. It does so by design and uses negligible resources.
- No GUI progress during install; replacing the few files takes well under a second.
