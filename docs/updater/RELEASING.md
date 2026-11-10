# Releasing FarSightVNC (owner procedure)

Every release that the in-app updater can install is a GitHub release of
`TheSampaio/FarSightVNC` with exactly three assets, one of them signed by the
owner's offline key. The design and the threat model are in `DESIGN.md`; the
manifest grammar is in `updater-lib/Manifest.h`; the scripts are described in
`tools/release/README.md`.

The public key that the viewer trusts is compiled into
`updater-lib/TrustedKeys.cpp`. The private key never enters the repository.

## 1. Prepare the version

1. Bump the version as `CLAUDE.md` requires (`tvnviewer/tvnviewer.rc` and
   `farsight-updater/farsight-updater.rc`, both numeric and string fields;
   `CLAUDE.md`, `AGENTS.md`, `README.md`, `MODIFICATIONS.md`).
2. Commit. The tag of the release must be exactly `v<version>` (for example
   `v1.0.0a`). The signed manifest version must equal the tag without the `v`,
   otherwise the viewer refuses the update.

## 2. Build

Release builds of all three projects, both platforms:

```powershell
powershell -NoProfile -Command "& .\tools\build.ps1 -Project updater-test, farsight-updater, tvnviewer -Configurations Release -Platforms x64,Win32"
```

(Pass the project list with `-Command`; `-File` breaks array arguments.) Run
`build\bin\<platform>\Release\updater-test.exe` for both platforms and
`tools\release\Test-ReleaseScripts.ps1`. All must pass before signing.

## 3. Shipping folder

```
FarSightVNC-v<version>\
  bin\x64\fsvnc-viewer.exe
  bin\x64\fsvnc-updater.exe
  bin\x86\fsvnc-viewer.exe
  bin\x86\fsvnc-updater.exe
  LICENSE.txt
  README.txt      (English)
  LEIAME.txt      (Portuguese)
```

`bin\x86` holds the 32-bit build (the MSBuild platform `Win32`); `bin\x64` the 64-bit one.

`README.txt` and `LEIAME.txt` carry the version and the changes of this release;
their sources are in `docs/shipping/`. Copy `fsvnc-viewer.exe` and
`fsvnc-updater.exe` from `build\bin\x64\Release` to `bin\x64` and from
`build\bin\Win32\Release` to `bin\x86`. Do not add any other
file: every file in the folder is installed on the user's machine and is listed
in the signed manifest (at most 64 files, each at most 64 MiB, paths of at most
four segments of `A-Z a-z 0-9 . _ -`).

## 4. Zip

The zip must be named `FarSightVNC-v<version>.zip`, with the entries at the
root (`bin/x64/fsvnc-viewer.exe`, ...) and `/` separators. Do not use
`Compress-Archive` (it writes `\` separators on Windows PowerShell 5.1).

```powershell
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$dir = "$env:USERPROFILE\Desktop\FarSightVNC-v1.0.0a"
$zip = "$dir.zip"
if (Test-Path $zip) { Remove-Item $zip }
$archive = [IO.Compression.ZipFile]::Open($zip, 'Create')
try {
  foreach ($file in Get-ChildItem $dir -Recurse -File) {
    $name = $file.FullName.Substring($dir.Length + 1).Replace('\', '/')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name, 'Optimal')
  }
} finally { $archive.Dispose() }
```

## 5. Sign

```powershell
.\tools\release\New-UpdateManifest.ps1 `
  -ShippingDirectory $env:USERPROFILE\Desktop\FarSightVNC-v1.0.0a `
  -ZipPath $env:USERPROFILE\Desktop\FarSightVNC-v1.0.0a.zip `
  -Version 1.0.0a `
  -PrivateKeyPath $env:USERPROFILE\.farsightvnc-signing\update-key-1.private.blob `
  -OutDirectory $env:USERPROFILE\Desktop
```

It writes `farsightvnc-update.manifest` and `farsightvnc-update.manifest.sig`
(64-byte raw signature), re-verifies the signature and checks that the zip holds
exactly the manifest's files with identical hashes. Never sign a version or a
package you did not build yourself, and never copy or print the private key.

## 6. Verify locally before publishing

The script already self-verifies. The strongest check is the production code
path against the real files, with no network:

```powershell
build\bin\x64\Release\updater-test.exe --stage-local v1.0.0a <folder with the three assets> <empty state dir> <previous version>
```

It runs `stageUpdate` with the compiled-in production keys on the local files and
exits 0 only if the signature, the version binding and every hash are accepted.
`tools\release\Test-EndToEndInstall.ps1` goes further: it installs the real
release over a copy of the previous shipping folder (waiting for a running
viewer, keeping user files, rejecting a tampered staging, refusing a downgrade).
Run it with `-ReleaseDir` (the folder holding the three assets) and
`-OldShipping` (an older shipping folder, in the current `bin\x64`/`bin\x86` layout or in
the legacy `x64`/`Win32` layout, which the script converts). Use `-Arch x86` to test the
32-bit build.

## 7. Publish on GitHub

1. Create the release with the tag exactly `v<version>`. Mark it **pre-release**
   for `a` and `b` versions; never for a stable version.
2. Upload the three assets with these exact names:
   - `FarSightVNC-v<version>.zip`
   - `farsightvnc-update.manifest`
   - `farsightvnc-update.manifest.sig`
3. Paste the release notes (template below). The viewer shows them in the update
   dialog, so keep them readable as plain text.
4. Do not edit or replace an asset after publishing; publish a new version
   instead (a replaced zip would no longer match the signed manifest).

### Release notes template

```markdown
## Security fixes
- ...

## Bug fixes
- ...

## Improvements
- ...

Updating: Help > Check for Updates (tray menu, window menu or About dialog), or
download the zip and extract it over the old folder. Versions older than the
first release with the updater must be updated manually once.
```

Omit an empty section, but keep the order. Describe security fixes for users, not
as an exploit recipe.

## 8. Key management

- **Backup.** Keep at least two offline copies of
  `%USERPROFILE%\.farsightvnc-signing\update-key-<id>.private.blob` (for example two
  encrypted USB drives kept in different places). Never store it in the repository,
  in cloud sync folders, in CI or in chat. The file is ACL-restricted to the owner
  (`New-UpdateSigningKey.ps1` does this).
- **Rotation.** Generate the next key with `tools\release\New-UpdateSigningKey.ps1
  -Id 2 -OutDirectory <outside the repository>`. Add the printed public key to
  `updater-lib/TrustedKeys.cpp` as id 2 next to key 1, and ship one release that
  is still signed with key 1. Releases after that can be signed with key 2; key 1
  can be removed from the code in a later release.
- **Suspected leak.** Treat it as lost (below) and rotate as soon as possible,
  because whoever holds the key can sign an update that every installed viewer
  accepts.
- **Lost key.** Installed viewers only trust keys compiled into them, so they can
  not be moved to a new key by an update signed with a key you no longer have. Generate
  a new key, publish a release whose `TrustedKeys.cpp` contains it, and tell users
  to download that release manually once (the viewer cannot install it; it
  shows the release page when a signature does not verify). From that release on,
  automatic updates work again with the new key.

## Release checklist

- [ ] Version bumped everywhere, committed, tag `v<version>` matches
- [ ] Release builds of `tvnviewer`, `farsight-updater`, `updater-test` (x64 and Win32), tests pass
- [ ] Shipping folder has exactly the files of section 3
- [ ] Zip made with `/` separators, entries at the root
- [ ] Manifest and signature created with the offline key, script reports verified
- [ ] `--stage-local` (and, if possible, the end-to-end script) accepted the real assets
- [ ] GitHub release (pre-release for a/b) with the three assets and the notes
