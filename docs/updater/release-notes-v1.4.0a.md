FarSightVNC v1.4.0a (alpha)

This is the seventh numbered release of FarSightVNC, a Windows VNC viewer based on TightVNC Viewer 2.8.90 (GPLv2-or-later).


> ⚠️ **Alpha build: a security release, and the local keyboard and file paste work that was never published.**
> - This release replaces **v1.2.1a** and keeps every feature and fix of that version. **v1.3.0a was prepared but never published**, so everything built for it arrives here: the local-keyboard work, the fixed favorites and the optional clipboard file paste.
> - Update with **Check for Updates** (Settings > Updates), or download the zip and extract it over the old folder. Your settings, favorites and `.vnc` files are not affected.
> - **This release changes what you see.** File transfer, and pasting files with Ctrl+V, now ask for confirmation once per session. A download whose file name would land outside the folder you chose is refused. Read *What's new* before you update a machine someone else depends on.

## What's new

- **You decide whether file transfer is offered, not the server.**
  - Under **Settings > Input > Restrictions** the new **File transfer** setting has three states: **Ask** (the default), **Allow** and **Never**.
  - With **Ask**, file transfer stays unavailable until you confirm, once per session, on your own action: the File Transfer button, the menu, the shortcut, or a Ctrl+V file paste. The answer holds for the rest of the session, and so does a refusal.
  - With **Never**, the viewer does not even tell the server it can transfer files. The connection succeeds and screen, keyboard and clipboard work normally.
  - Nothing the server sends can raise that prompt, and it appears at most once per session, so a hostile server cannot use it to pester you.
  - The setting is stored per connection and a configuration that does not carry it still loads, so your existing profiles and `.vnc` files keep working.
- **Hand the keyboard back to your own computer.** A **Release keyboard** button in the session toolbar, the full-screen toolbar and the session menus gives keyboard input to Windows, modifier keys included. Click it again to resume typing on the remote computer; keys held on the remote side are released when you switch.
- **Add key (Settings > Input)** captures the key-down before ordinary Windows hotkey handling. For a screenshot shortcut on **Numpad Minus**, add that exact key: the regular minus key is a different key.
- **Print Screen is local by default**, now as an editable entry rather than a hard-coded exception. Remove it from the list when you want Print Screen to reach the remote computer. Local-key entries match single keys; for combinations that need local modifiers, use **Release keyboard**.
- **Favorites stay fixed** on the Home screen while only the recent connections scroll.
- **Optional clipboard file paste.** Copy files or folders locally and press Ctrl+V in a session to a server that advertises the FarSightVNC **FSVC** clipboard-file capability. The viewer stages the upload, waits for the remote clipboard to be confirmed, then sends the paste keystroke. **Existing TightVNC servers do not provide this**, and with them normal clipboard and Ctrl+V behaviour continue unchanged, with no warning and no new server requirement.

## Fixes

This release fixes the problems found in a security audit of the code. The serious ones are listed under *Security*; these are the rest.

- **A download can no longer delete a folder.** If the target of a download is a folder that already exists, the download now fails with a clear error instead of the folder being removed and replaced by a file.
- **Emptying a file no longer deletes it first.** The viewer used to delete the file and create it again, which destroyed the old content before knowing the new file could be created. It now truncates the file in place and reports failure instead of carrying on.
- **Skip is the default button** when a download would overwrite an existing file, instead of Overwrite, so a stray Enter on a dialog you did not expect cannot destroy a file of yours.
- **An invalid screen format no longer kills the viewer.** A server announcing an 8-bit-per-pixel format with a zero colour maximum used to crash the process, losing every session in that window. Such a format is now refused with a message when the session starts.
- **Malformed data ends the session cleanly.** When a length or a count from the server does not make sense, the session now ends with a message instead of the viewer carrying on and reading the stream out of step. Just reconnect.
- **A large file listing can no longer exhaust the viewer's stack**, which used to be possible when many entries in a row failed.

## Security

This is the reason for the release. The findings come from an audit of the code dated 2026-11-13 and from an independent review of the fixes. **Every problem below was inherited from TightVNC Viewer 2.8.90 and is not specific to FarSightVNC**, except where stated.

- **A hostile server could write files anywhere you can write (critical).** The viewer took the file name of every entry in a remote listing and built the local path from it with no checking at all, and the confirmation dialog only appeared when the target already existed. A server could return a name such as `..\..\..\Startup\x.lnk` and get a file into your Windows startup folder from a single download you approved, with no prompt. Now the name is checked where it is parsed, a rejected entry is skipped and logged, and the finished path must be inside the folder you chose. The write handle also refuses to follow a symbolic link or junction planted at the target.
- **A hostile server could read the viewer's memory into your files.** A reply could claim more uncompressed data than it actually sent, and the viewer wrote the difference out of adjacent process memory into the file on your disk; it can contain your VNC password in plain text, clipboard contents and parts of the screen. The same mismatch fed memory into the file names shown on screen. The size of the data that really arrived is now the only number used.
- **Every length and count that arrives from the server now has a limit**, checked before any memory is reserved, with large payloads read in bounded pieces. One of the old checks could never be true on a 64-bit build, so it was doing nothing at all.
- **A reply that arrives out of turn is refused** instead of being acted on, which used to crash the viewer.
- **Allocation failures no longer leave the viewer freeing the same memory twice.**
- No change to the VNC protocol, to authentication, to the encodings, or to anything on the server side. Every new check is one a correct TightVNC server already satisfies.
- **This release adds no encryption, and that remains the largest open risk.** The VNC protocol used with TightVNC servers has no encryption and no server authentication, so anyone positioned on the network path can read and alter a session. There is no way for the viewer to require encryption and still work with the servers you already have. **On a network you do not trust, use a VPN or an SSH tunnel.**
- Two limits worth knowing: with the setting on **Ask** the viewer still tells the server it can transfer files, because that is negotiated when the connection opens, so before you confirm a hostile server can still send file transfer replies — what protects you there is the checking described above, not the switch. And **Overwrite All** still applies to the whole download rather than only the files you saw, so a folder listing can bring entries it covers.
- The signed-manifest mechanism of the updater is unchanged: an ECDSA P-256 signature over the manifest checked against a public key built into the program, a size and SHA-256 check of the package and of every file before anything is installed, a refusal of downgrades, and a private signing key that is in no repository or release file.

## Tests

- Automated, from the official x64 Release build of this version: security 418/0, clipboard file paste 171/0, keyboard 548/0, input queue 39/0, favorites 146/0, updater 2384 checks/0. The CMake suite passes 8 of 8.
- The security tests were checked against the unfixed code: disabling the new checks on purpose makes them fail, and removing the memory fix makes the process die, so they are testing the fixes and not just themselves. The file transfer tests also ran clean under AddressSanitizer.
- The keyboard end-to-end runs (46 cases each, local and remote Print Screen) and the release script checks were run when the keyboard work was written, **not against this build**.
- **Not verified in this version:** a real TightVNC Server, a real session on another computer, the three states of the file transfer setting at run time, the refusal of a file symbolic link (planting one needs Windows Developer Mode), the look of the new setting on screen, Application Verifier (it needs elevation), Windows 10, several monitors, screen readers, and an installation from a published release.

## Downloads

| File | Content |
|---|---|
| `FarSightVNC-v1.4.0a.zip` | Windows 64-bit viewer, updater and documentation |
| `farsightvnc-update.manifest` | Signed update manifest used by the updater |
| `farsightvnc-update.manifest.sig` | Manifest signature used by the updater |

Keep the manifest and its signature attached to the release. Without them the updater cannot install this version automatically.

## Known limitations

- **No encryption.** See *Security*. Use a VPN or an SSH tunnel on an untrusted network.
- Updating **from v1.2.0a or older** runs the old updater, so FarSightVNC may not restart by itself after that one update.
- A download target that is an existing folder, or a junction, is now refused rather than used. This is deliberate, but it is a change: a folder you deliberately keep as a junction inside your download folder no longer works as a target.
- A server that sends a file name Windows resolves to something else (a trailing dot or space, a reserved device name such as `NUL`) has that entry dropped from the listing. A correct TightVNC server never sends one.
- A refusal of the file transfer prompt lasts the session. If you answer no by mistake, reconnect or set the setting to **Allow**.
- A Ctrl or Shift held *before* you activate the viewer reaches the remote computer with its first key repeat, so a Ctrl+click right after Alt+Tab into the viewer can arrive as a plain click.
- A local shortcut matches the bare key. Combinations such as Ctrl+Numpad Minus are not kept local, and Numpad digits depend on Num Lock.
- Clipboard file paste needs FarSightVNC on both ends and stays dormant against a TightVNC server.
- If the remote TightVNC Server runs with lower privileges than the focused remote window (an administrator console, UAC), the server may fail to release a key; that behaviour belongs to the server and is not changed here.
- The updater uses a folder you can write to; when the folder needs administrator rights, Windows asks for approval once at install time.
- **SmartScreen:** the executables are not Authenticode-signed, so Windows may show a warning on first launch.
- **Shared settings:** viewer settings are still shared with TightVNC Viewer (`HKCU\Software\TightVNC\Viewer\`); the local shortcut list is stored there as `LocalKeys` and the new file transfer setting as `farsight_filetransfer`. The original TightVNC Viewer ignores entries it does not know, so the files stay interchangeable.
- Installer sources for a per-user installation are in the repository, but the Setup has not been compiled or tested, so it is not attached here. The portable zip keeps the five-file flat layout.

## Documentation

- **Portuguese:** `LEIAME.txt`
- **English:** `README.txt`
- **Full release notes:** `docs/updater/release-notes-v1.4.0a.md`
- **Security audit:** `docs/security/FARSIGHT_SECURITY_AUDIT.md`
- **Remediation matrix:** `docs/security/REMEDIACAO.md`

**Source:** based on TightVNC Viewer 2.8.90 (GPLv2-or-later). TightVNC copyright © GlavSoft LLC is retained. Icons by Lucide (ISC License, see `vendor/lucide/LICENSE`). The full list of modifications is in `MODIFICATIONS.md`.
