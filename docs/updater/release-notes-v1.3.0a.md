FarSightVNC v1.3.0a (alpha)

## Local keyboard and screenshots

- **Add key** in Settings > Input captures the key-down before ordinary Windows hotkey handling. For a screenshot shortcut assigned to **Numpad Minus**, add that exact key; the regular minus key is a separate key.
- A **Release keyboard** button in the session toolbar, full-screen toolbar and session menus hands keyboard input to the local computer. Click it again to resume remote typing. Held remote keys are released when switching to local input.
- **Print Screen** is local by default and can be removed from the local-key list when remote Print Screen is wanted. Local-key exclusions apply to individual keys; use Release keyboard for combinations that need local modifiers.

## Home

- Favorite computers stay fixed while recent connections scroll.

## Optional clipboard file paste

- Copy files or folders locally and press Ctrl+V in a session to a server that advertises the **FarSightVNC FSVC clipboard-file capability**. The viewer stages the upload, waits for confirmation of the remote clipboard, then sends the paste keystroke.
- This extension requires support on both ends. **Existing TightVNC servers do not provide it.** Their normal clipboard and Ctrl+V behavior continue to work without a new server, warning or protocol requirement.
- The distribution contains only the viewer and updater. No VNC server or service is installed.

## Distribution status

These are candidate release notes. Build, validation, shipping and publication status are recorded in `docs/reports/continuidade-codex-2026-11-16.md`.

The portable zip keeps the five-file flat layout. Installer sources and their prerequisites are documented in `docs/installer/`; an installer must be compiled and tested before being attached to a release.

Settings, favorites, `.vnc` files and TightVNC connection options are preserved. Based on TightVNC Viewer 2.8.90, GPL version 2 or later.
