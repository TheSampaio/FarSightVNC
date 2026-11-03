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
| 2026-11-02 | `tvnviewer/{GpuPresenter.*,RenderSettings.h,DesktopWindow.*,ViewerCmdLine.*,tvnviewer.vcxproj*}`, `tools/gpu-presenter-test.*` | Optional Direct3D 11 / Direct2D presentation (GPU scaling of the DIB frame buffer into a bit-blt swap chain, run-time loaded d3d11/d2d1, automatic GDI fallback on device loss or unsupported formats, disabled after 3 consecutive failures). Selected with `-renderer=gdi\|auto\|gpu`, `FARSIGHTVNC_RENDERER` or HKCU `Software\TightVNC\Viewer\Settings\Renderer`; the default stays GDI. Native regression test `tools/gpu-presenter-test.ps1`. |
| 2026-11-02 | `docs/reports/fase-3-libjpeg-turbo.md` | New file: written evaluation of libjpeg-turbo (no dependency or code change; decision deferred to Tight/JPEG profiling). |
| 2026-11-02 | `tvnviewer/{GpuPresenter.*,DesktopWindow.cpp}`, `tools/gpu-presenter-test.cpp` | The GPU path creates its Direct3D device on a worker thread (`GpuPresenter::warmUp`) and GDI paints until it is ready: the first present used to stall the window thread for about 190 ms, which dropped the first posted keys in the keyboard end-to-end test in GPU mode. |
| 2026-11-02 | `docs/reports/fase-3-relatorio.md` | New file: Phase 3 report (input queue, GPU presenter, measurements, pending lock-hold item). |
| 2026-11-02 | `zlib/*` | zlib updated from 1.2.13 to 1.3.2 (security and bug fixes). Upstream files copied verbatim from the official `zlib-1.3.2.tar.gz` (SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`, GPG signature by Mark Adler, key `5ED46A6721D365587791E2AA783FCD8E58BCAFBA`, verified), CRLF line endings kept; `zlib.vcxproj` unchanged. No FarSightVNC notice is added inside upstream zlib files. |
| 2026-11-02 | `tools/long-session.ps1` | New file: long-running session test that samples viewer private memory, handles and GDI/USER objects per renderer against the loopback test server. |
