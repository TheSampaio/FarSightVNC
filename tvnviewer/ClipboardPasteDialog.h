// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a modified version of TightVNC.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Added for FarSightVNC on 2026-11-16: clipboard file paste (local viewer
// machine -> remote server machine).
//

#ifndef _CLIPBOARD_PASTE_DIALOG_H_
#define _CLIPBOARD_PASTE_DIALOG_H_

#include "gui/BaseDialog.h"
#include "gui/Control.h"
#include "gui/ProgressBar.h"

class ClipboardPasteDialog;

// What the dialog reports back to the paste it belongs to. Every method is
// called on the thread of the dialog window (the viewer user interface
// thread).
class ClipboardPasteDialogListener
{
public:
  // The user pressed Cancel.
  virtual void onPasteCancelled() = 0;
  // The staging thread has finished. The dialog is still open; the listener
  // closes it.
  virtual void onPasteFinished(bool ok) = 0;

protected:
  ~ClipboardPasteDialogListener() {}
};

// Progress of one clipboard file paste, with a Cancel button. It follows the
// file transfer dialog: the same modeless BaseDialog, the same progress bar
// control, only the two controls this one needs.
class ClipboardPasteDialog : public BaseDialog
{
public:
  // Posted by the staging thread; wParam is the progress in tenths of a
  // percent (0 to 1000).
  static const UINT WM_PASTE_PROGRESS = WM_USER + 1;
  // Posted by the staging thread when it is done; wParam is 1 on success.
  static const UINT WM_PASTE_FINISHED = WM_USER + 2;
  // Posted by the staging thread; wParam is one of the STATUS_ values.
  static const UINT WM_PASTE_STATUS = WM_USER + 3;

  // Steps the user is told about, in the order they happen.
  static const WPARAM STATUS_MEASURING = 1;
  static const WPARAM STATUS_STAGING_FOLDER = 2;
  static const WPARAM STATUS_COPYING = 3;
  static const WPARAM STATUS_CLIPBOARD = 4;
  static const WPARAM STATUS_CANCELLING = 5;

  ClipboardPasteDialog(Control *parent, ClipboardPasteDialogListener *listener);
  virtual ~ClipboardPasteDialog();

  // Text above the progress bar. Only called from the thread of the window.
  void setStatusText(const TCHAR *text);
  // Text of one of the STATUS_ values.
  static const TCHAR *statusText(WPARAM status);

protected:
  virtual BOOL onInitDialog();
  virtual BOOL onCommand(UINT controlID, UINT notificationID);
  virtual BOOL onClose();
  virtual void onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
  ClipboardPasteDialogListener *m_listener;

  Control m_status;
  ProgressBar m_progressBar;

  StringStorage m_pendingStatus;
};

#endif // _CLIPBOARD_PASTE_DIALOG_H_
