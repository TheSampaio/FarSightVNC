// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// The update notification dialog: one modeless window that walks through
// checking, available, downloading and ready (and the failure states). It
// holds no update logic; UpdateController feeds it and receives its requests.

#ifndef _UPDATE_DIALOG_H_
#define _UPDATE_DIALOG_H_

#include "gui/BaseDialog.h"

#include <cstdint>
#include <string>

class UpdateDialog : public BaseDialog
{
public:
  enum class State {
    Checking,
    UpToDate,
    Failed,      // text = reason
    Disabled,
    Available,   // see AvailableInfo
    Downloading, // text = headline, progress via setProgress()
    Ready,
    StageFailed, // text = reason
  };

  // How present() treats the window.
  enum class Focus {
    Keep,       // update an open window as it is; open a closed one unfocused
    Activate,   // show and bring to the front
    NoActivate, // show without taking the focus (automatic results)
  };

  struct AvailableInfo {
    std::wstring currentVersion;
    std::wstring availableVersion;
    std::wstring releaseDate;   // YYYY-MM-DD or empty
    std::wstring channel;       // e.g. "alpha (pre-release)"
    std::wstring notes;         // CRLF separated, already capped
    std::wstring releaseUrl;    // empty -> no "View on GitHub" link
    std::wstring note;          // extra line (elevation hint, ...)
    bool canInstall = true;     // false -> "cannot be installed automatically"
  };

  // Requests of the user, implemented by UpdateController.
  class Host
  {
  public:
    virtual ~Host() {}
    virtual void onUpdateDownloadRequested() = 0;
    virtual void onUpdateCancelDownload() = 0;
    virtual void onUpdateCancelStaged() = 0;
    virtual void onUpdateSkipRequested() = 0;
    virtual void onUpdateAutoCheckToggled(bool enabled) = 0;
    virtual void onUpdatePrereleaseToggled(bool enabled) = 0;
  };

  explicit UpdateDialog(Host *host);
  virtual ~UpdateDialog();

  void setOptions(bool autoCheck, bool includePrereleases);
  void setAvailable(const AvailableInfo &info);
  // Creates the window if needed. text is the message of the states that have one.
  void present(State state, const std::wstring &text, Focus focus);
  void setProgress(std::uint64_t received, std::uint64_t total);
  // Shows the current state again (e.g. to bring a running operation forward).
  void reopen(Focus focus);

  State state() const { return m_state; }
  // Closes the window (no-op if it is not open).
  void close();

protected:
  virtual BOOL onInitDialog();
  virtual BOOL onNotify(UINT controlID, LPARAM data);
  virtual BOOL onCommand(UINT controlID, UINT notificationID);
  virtual BOOL onDestroy();

private:
  void apply();
  void layout(const UINT *buttons, int buttonCount, UINT defaultButton, int contentBottom);
  void showControl(UINT id, bool visible);
  void moveControl(UINT id, int x, int y, int width, int height);
  void setText(UINT id, const std::wstring &text);
  void updateProgressControls();
  void openUrl(const std::wstring &url);

  Host *m_host;
  State m_state;
  std::wstring m_text;
  AvailableInfo m_available;
  bool m_autoCheck;
  bool m_includePrereleases;
  std::uint64_t m_received;
  std::uint64_t m_total;
  bool m_marquee;
};

#endif
