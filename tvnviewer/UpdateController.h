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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class UpdateController
{
public:
  // What the controller needs from the application (TvnViewer).
  class Host
  {
  public:
    struct Shell
    {
      HWND window;
      bool fullscreen;
    };
    virtual ~Host() {}
    // The visible, live shell windows of this process.
    virtual std::vector<Shell> updateShells() = 0;
    // One line in the viewer log (automatic check outcomes and the like).
    virtual void updateLog(const std::wstring &line) = 0;
    // Shell windows that show a session (connected or connecting).
    virtual int updateOpenSessions() = 0;
    // "Restart and Install Now": stops listening and closes every shell
    // window through its normal close path, so that the process exits and
    // main() reaches launchPendingUpdaterAtExit(). Kills nothing.
    virtual void updateCloseAllWindows() = 0;
  };

  // mainWindow is the hidden window of TvnViewer; it receives the messages of
  // the worker thread and the timers. Nothing slow happens here.
  UpdateController(HWND mainWindow, Host *host);
  // Cancels the running operation and joins the worker thread.
  ~UpdateController();

  UpdateController(const UpdateController &) = delete;
  UpdateController &operator=(const UpdateController &) = delete;

  // Periodic timer (first tick shortly after start): automatic check if the
  // policy says it is due. A long-lived process thus checks again after the
  // interval; the policy and a cross-process lock keep it to one check per
  // interval for all viewer processes of the user.
  void onAutoCheckTimer();
  // Short timer while an automatic result waits for full screen to end.
  void onDeferredTimer();
  // "Check for Updates..." menu entries. Runs unless disabled by policy.
  void requestManualCheck();

  // Update settings shown by the Settings page.
  struct Preferences
  {
    bool available;          // false: this build cannot be updated
    bool disabledByPolicy;   // the administrator disabled the checks
    bool autoCheck;
    bool includePrereleases; // effective value (default: channel of this build)
    std::uint64_t lastCheckUnix; // 0: never
    std::wstring currentVersion;
  };
  Preferences preferences();
  void setAutoCheck(bool enabled);
  void setIncludePrereleases(bool enabled);

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
// fsvncupdater.exe detached if an update is staged or an interrupted
// installation must be repaired. Does nothing otherwise. After "Restart and
// Install Now" it passes --relaunch, so the updater starts the viewer again.
void launchPendingUpdaterAtExit();

#endif
