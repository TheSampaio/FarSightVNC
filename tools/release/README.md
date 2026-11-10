# Release signing tools

Windows PowerShell 5.1, no extra modules. The design is in `docs/updater/DESIGN.md`;
the manifest grammar is in `updater-lib/Manifest.h`.

## Scripts

- `New-UpdateSigningKey.ps1 -OutDirectory <dir> [-Id 1]`
  Generates an ECDSA P-256 key pair: `update-key-<Id>.private.blob` (ACL restricted to the
  current user) and `update-key-<Id>.public.blob` (72-byte `BCRYPT_ECCPUBLIC_BLOB`). It prints
  the public key as a C++ array for `updater-lib/TrustedKeys.cpp`. It refuses to write inside the
  repository or to overwrite existing files. Keep the private blob offline and back it up
  (for example in `%USERPROFILE%\.farsightvnc-signing\`). Never commit it.
- `New-UpdateManifest.ps1 -ShippingDirectory <dir> -ZipPath <zip> -Version <x.y.z[a|b]> -PrivateKeyPath <blob> [-OutDirectory <dir>]`
  Lists every file of the shipping folder with size and SHA-256, adds the package, writes the
  manifest (UTF-8, no BOM, LF) and signs its exact bytes. It self-verifies the signature and checks
  that the zip holds exactly the same files with identical hashes. Any mismatch exits with code 1.
- `Test-ReleaseScripts.ps1 [-TestExe <path>]`
  End-to-end check against `updater-test.exe` (default `build\bin\x64\Release\updater-test.exe`):
  throw-away key, fake flat shipping folder (`fsvncviewer.exe`, `fsvncupdater.exe`, docs) and zip,
  manifest verified by the C++ code, tampering rejected. Uses `%TEMP%` and cleans up.
- `Test-EndToEndInstall.ps1 -ReleaseDir <dir> -OldShipping <dir>`
  Installs the real signed release (x64 only) over a copy of an older version: a Full layout
  install (waits for the running old viewer, keeps user files), a Standalone install (no
  `LICENSE.txt`: only the two executables are written, a user `README.txt` stays untouched), a
  tampered staging (rejected) and a downgrade (refused). `-OldShipping` is a flat folder or a
  legacy `x64\tvnviewer.exe` folder, whose executable is copied as `fsvncviewer.exe` to build the
  "old installation". Uses `%TEMP%` and cleans up.

## Release assets (per GitHub release)

| Asset | Content |
|---|---|
| `FarSightVNC-v<version>.zip` | the flat shipping layout, only at the zip root: `fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt`, `README.txt` (64-bit only) |
| `farsightvnc-update.manifest` | signed manifest |
| `farsightvnc-update.manifest.sig` | raw 64-byte P1363 signature of the manifest bytes |

The manifest version must equal the release tag without the `v`.
