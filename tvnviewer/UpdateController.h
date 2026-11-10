// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Viewer side of the auto-updater (see docs/updater/DESIGN.md, flows 1-4 and 6).
// Owned by TvnViewer. All methods run on the UI thread of the hidden main
// window; the only other thread is the worker that talks to GitHub, and it
// reports back with posted messages (TvnViewer::WM_USER_UPDATE_*).

#ifndef _UPDATE_CONTROLLER_H_
#define _UPDATE_CONTROLLER_H_

#include "util/winhdr.h"

#include <memory>

class UpdateController
{
public:
  // mainWindow is the hidden window of TvnViewer; it receives the messages of
  // the worker thread. Nothing slow happens here.
  explicit UpdateController(HWND mainWindow);
  // Cancels the running operation and joins the worker thread.
  ~UpdateController();

  UpdateController(const UpdateController &) = delete;
  UpdateController &operator=(const UpdateController &) = delete;

  // One-shot startup timer: automatic check if the policy says it is due.
  void onStartupTimer();
  // "Check for Updates..." menu entries. Runs unless disabled by policy.
  void requestManualCheck();

  // Messages posted by the worker thread.
  void onCheckDone();
  void onProgress();
  void onStageDone();

  // True if an interrupted installation was found (repaired at exit).
  bool interruptedUpdateFound();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// Called by main() after the viewer has been destroyed: starts
// fsvnc-updater.exe detached if an update is staged or an interrupted
// installation must be repaired. Does nothing otherwise.
void launchPendingUpdaterAtExit();

#endif
