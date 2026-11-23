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

#include "ClipboardPasteDialog.h"
#include "resource.h"

ClipboardPasteDialog::ClipboardPasteDialog(Control *parent,
                                           ClipboardPasteDialogListener *listener)
: m_listener(listener)
{
  setResourceId(IDD_CLIPBOARD_PASTE);
  setParent(parent);
}

ClipboardPasteDialog::~ClipboardPasteDialog()
{
}

BOOL ClipboardPasteDialog::onInitDialog()
{
  HWND hwnd = m_ctrlThis.getWindow();
  m_status.setWindow(GetDlgItem(hwnd, IDC_CLIPPASTE_STATUS));
  m_progressBar.setWindow(GetDlgItem(hwnd, IDC_CLIPPASTE_PROGRESS));
  m_progressBar.setRange(0, 1000);
  m_progressBar.setPos(0);
  if (!m_pendingStatus.isEmpty()) {
    m_status.setText(m_pendingStatus.getString());
  }
  m_theme.attach(hwnd);
  return TRUE;
}

void ClipboardPasteDialog::setStatusText(const TCHAR *text)
{
  m_pendingStatus.setString(text);
  if (isCreated()) {
    m_status.setText(text);
  }
}

const TCHAR *ClipboardPasteDialog::statusText(WPARAM status)
{
  switch (status) {
  case STATUS_MEASURING:
    return _T("Preparing the files to paste...");
  case STATUS_STAGING_FOLDER:
    return _T("Asking the remote computer for a staging folder...");
  case STATUS_COPYING:
    return _T("Copying the files to the remote computer...");
  case STATUS_CLIPBOARD:
    return _T("Setting the clipboard of the remote computer...");
  case STATUS_CANCELLING:
    return _T("Cancelling...");
  }
  return _T("");
}

BOOL ClipboardPasteDialog::onCommand(UINT controlID, UINT notificationID)
{
  if (controlID == IDCANCEL) {
    // The window stays open until the staging thread has really stopped, so
    // that the user is not left wondering whether anything is still running.
    m_status.setText(statusText(STATUS_CANCELLING));
    m_listener->onPasteCancelled();
  }
  return TRUE;
}

BOOL ClipboardPasteDialog::onClose()
{
  m_listener->onPasteCancelled();
  return TRUE;
}

void ClipboardPasteDialog::onMessageReceived(UINT uMsg, WPARAM wParam,
                                             LPARAM lParam)
{
  switch (uMsg) {
  case WM_PASTE_PROGRESS:
    m_progressBar.setPos(static_cast<WORD>(wParam));
    break;
  case WM_PASTE_STATUS:
    setStatusText(statusText(wParam));
    break;
  case WM_PASTE_FINISHED:
    m_listener->onPasteFinished(wParam != 0);
    break;
  }
}
