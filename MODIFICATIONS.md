# FarSightVNC — modification notice

FarSightVNC is a modified version of **TightVNC 2.8.90**, Copyright (C)
GlavSoft LLC, distributed under the GNU General Public License version 2 or
(at your option) any later version. See `wix-installer/LICENSE.txt` for the
license text. The original copyright and license notices in every file are
preserved.

As required by section 2(a) of the GPL, this file lists the changes made to
the original work and their dates. Source files that were changed also carry a
short "Modified for FarSightVNC" notice in their header. The complete,
line-level history is in the Git log; the first commit is the unmodified
TightVNC 2.8.90 source tree.

| Date | Files | Change |
|---|---|---|
| 2026-10-29 | all `*.vcxproj` | Build retargeted from MSVC v141_xp / Windows SDK 8.1 to MSVC v145 / Windows SDK 10. Windows XP is no longer a supported target. |
| 2026-10-29 | `rfb/RfbKeySym.h` | Added missing `<string>` include. No behavior change. |
| 2026-10-29 | `.gitignore`, `tools/*.ps1`, `docs/` | New files: build, test and measurement scripts and project documentation. |
| 2026-11-01 | `rfb/RfbKeySym.*`, `win-system/WinHooks.*`, `tvnviewer/{DesktopWindow,TvnViewer,ViewerWindow}.cpp`, `viewer-keyboard-test/`, `tightvnc2017.sln` | Symmetric physical key tracking, ordered dead-key translation, hook focus/filtering, full focus release and deterministic keyboard regression harness. |
| 2026-11-02 | `rfb/RfbKeySym.cpp`, `win-system/WinHooks.*`, `tvnviewer/{DesktopWindow,TvnViewer}.cpp`, `viewer-keyboard-test/`, `tools/{keyboard-e2e-test.ps1,fake-rfb-keyrecorder.py}` | Phase 1 review fixes: held Win key release, hook ownership across viewer windows, keyboard thread state in view-only mode, toolbar Ctrl/Alt latch; keyboard end-to-end test with a key recording fake RFB server. |
| 2026-11-02 | `gui/{BaseWindow,ScrollBar,ToolBar,DpiUtils}.*`, GUI projects, viewer DPI manifest/project, `tvnviewer/{DesktopWindow,FileInfoListView,ScaleManager,ViewerWindow}.*`, `tools/{fake-rfb-server,phase2-test}.ps1` | Phase 2 review snapshot: per-monitor DPI, real framebuffer coordinates, horizontal wheel and display/fullscreen monitor controls. |
