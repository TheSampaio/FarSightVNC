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
| 2026-11-02 | `tvnviewer/{ViewerWindow,DesktopWindow}.*`, `tools/phase2-test.ps1`, `docs/reports/fase-2-relatorio.md` | Phase 2 review fixes: restored window size follows `WM_DPICHANGED`, default window (frame and toolbar) fits the work area, Display-menu monitor entries enter fullscreen and the monitor list follows `WM_DISPLAYCHANGE`, background-dirty check uses the real server size; `-DisplayModes` harness checks. |
| 2026-11-01 | `tools/fake-rfb-server.py`, `tools/render-probe.py`, `tools/measure-render.ps1` | New files: fake RFB server, input/display latency probe and render benchmark orchestrator for the rendering and latency phase. |
| 2026-11-02 | `viewer-core/{InputEventSender.*,RemoteViewerCore.*,TcpConnection.cpp,viewer-core.vcxproj*}`, `viewer-input-queue-test/`, `tightvnc2017.sln` | Keyboard, pointer and cut text messages are queued and written by a dedicated thread (strict FIFO, key events never dropped, queued releases flushed on disconnect) instead of the UI thread; TCP_NODELAY on every viewer socket, including listening mode; deterministic queue test project. |
| 2026-11-02 | `viewer-core/{InputEventSender.*,RemoteViewerCore.cpp,TcpConnection.cpp}`, `viewer-input-queue-test/` | Input queue review fixes: button transitions keep their own position (only pure moves merge), `stop()` waits up to 250 ms for queued releases before closing, `TcpConnection::close()` also shuts down accepted (reverse) sockets, queue test covers bounded drain, real loopback TCP close and fail-fast watchdogs (port 5923 only). |
| 2026-11-02 | `tvnviewer/ViewerInstance.cpp` | The destructor no longer shuts down the socket before `RemoteViewerCore::stop()`, so queued key releases can drain first; `stop()` now closes accepted reverse sockets too. |
