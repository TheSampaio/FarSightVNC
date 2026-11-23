// Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
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

#include "ReconnectDialog.h"
#include "ReconnectPolicy.h"

#include "DialogTheme.h"
#include "gui/BaseDialog.h"
#include "resource.h"

#include <algorithm>
#include <stdio.h>
#include <string>

namespace {

const UINT_PTR COUNTDOWN_TIMER = 1;
const UINT COUNTDOWN_TICK_MS = 200;

typedef std::basic_string<TCHAR> Text;

// The prompt of ReconnectDialog::show(). Button ids and results are the ones
// the task dialog had; Esc and the close box answer "do not reconnect".
class ReconnectPrompt : public BaseDialog
{
public:
  ReconnectPrompt(const TCHAR *caption, const TCHAR *instruction, const TCHAR *reason,
                  unsigned int autoDelayMs, unsigned int nextAttempt, unsigned int maxAttempts,
                  bool centerOnOwner)
  : BaseDialog(IDD_RECONNECT),
    m_caption(caption != 0 ? caption : _T("")),
    m_instruction(instruction != 0 ? instruction : _T("")),
    m_reason(reason != 0 ? reason : _T("")),
    m_delayMs(autoDelayMs),
    m_nextAttempt(nextAttempt),
    m_maxAttempts(maxAttempts),
    m_autoEnabled(autoDelayMs != 0),
    m_clicked(false),
    m_centerOnOwner(centerOnOwner),
    m_startTick(0),
    m_shownSeconds(0)
  {
  }

protected:
  BOOL onInitDialog()
  {
    HWND window = m_ctrlThis.getWindow();
    m_theme.setHeading(IDC_RECONNECT_INSTRUCTION);
    m_theme.attach(window);
    if (!m_caption.empty()) {
      SetWindowText(window, m_caption.c_str());
    }
    SetDlgItemText(window, IDC_RECONNECT_INSTRUCTION, m_instruction.c_str());

    if (m_delayMs == 0) {
      // Without a countdown there is nothing to stop: Reconnect takes the
      // place of the Stop retrying button, next to Close.
      moveReconnectBeforeClose(window);
      HWND stop = GetDlgItem(window, ReconnectDialog::ID_STOP);
      EnableWindow(stop, FALSE);
      ShowWindow(stop, SW_HIDE);
    }
    m_startTick = GetTickCount();
    refreshContent(ReconnectPolicy::secondsLeft(m_delayMs, 0));
    fitText(IDC_RECONNECT_INSTRUCTION);
    if (m_centerOnOwner) {
      centerOnOwner(window);
    }
    // The prompt can come from a thread without an active window.
    setForeground();
    SetFocus(GetDlgItem(window, ReconnectDialog::ID_RECONNECT));
    if (m_delayMs != 0) {
      SetTimer(window, COUNTDOWN_TIMER, COUNTDOWN_TICK_MS, 0);
    }
    return FALSE;
  }

  BOOL onCommand(UINT controlID, UINT notificationID)
  {
    if (notificationID != BN_CLICKED) {
      return FALSE;
    }
    HWND window = m_ctrlThis.getWindow();
    switch (controlID) {
    case ReconnectDialog::ID_RECONNECT:
    case ReconnectDialog::ID_CLOSE:
    case IDCANCEL:
      kill(controlID);
      return TRUE;
    case ReconnectDialog::ID_STOP:
      // Stops the countdown; the prompt stays open.
      m_autoEnabled = false;
      refreshContent(0);
      EnableWindow(GetDlgItem(window, ReconnectDialog::ID_STOP), FALSE);
      SetFocus(GetDlgItem(window, ReconnectDialog::ID_RECONNECT));
      return TRUE;
    }
    return FALSE;
  }

  BOOL onClose()
  {
    kill(IDCANCEL);
    return TRUE;
  }

  BOOL onDestroy()
  {
    KillTimer(m_ctrlThis.getWindow(), COUNTDOWN_TIMER);
    return FALSE;
  }

  void onMessageReceived(UINT message, WPARAM wParam, LPARAM)
  {
    if (message != WM_TIMER || wParam != COUNTDOWN_TIMER || !m_autoEnabled ||
        m_clicked || m_delayMs == 0) {
      return;
    }
    unsigned int left = ReconnectPolicy::secondsLeft(m_delayMs, GetTickCount() - m_startTick);
    if (left == 0) {
      m_clicked = true;
      PostMessage(m_ctrlThis.getWindow(), WM_COMMAND,
                  MAKEWPARAM(ReconnectDialog::ID_RECONNECT, BN_CLICKED), 0);
    } else if (left != m_shownSeconds) {
      refreshContent(left);
    }
  }

private:
  Text buildContent(unsigned int secondsLeft) const
  {
    Text content = m_reason;
    if (m_delayMs == 0) {
      return content;
    }
    TCHAR line[160];
    if (m_autoEnabled) {
      _stprintf_s(line, _T("Reconnecting automatically in %u s (attempt %u of %u)."),
                  secondsLeft, m_nextAttempt, m_maxAttempts);
    } else {
      _stprintf_s(line, _T("Automatic reconnection stopped."));
    }
    if (!content.empty()) {
      content += _T("\n\n");
    }
    content += line;
    return content;
  }

  void refreshContent(unsigned int secondsLeft)
  {
    m_shownSeconds = secondsLeft;
    Text content = buildContent(secondsLeft);
    SetDlgItemText(m_ctrlThis.getWindow(), IDC_RECONNECT_CONTENT, content.c_str());
    fitText(IDC_RECONNECT_CONTENT);
  }

  static RECT rectIn(HWND window, HWND control)
  {
    RECT rect;
    GetWindowRect(control, &rect);
    MapWindowPoints(0, window, reinterpret_cast<POINT *>(&rect), 2);
    return rect;
  }

  // Gives the label the height of its text (word wrapped at its width) and
  // moves everything below it, and the bottom of the dialog, by the
  // difference.
  void fitText(UINT labelId)
  {
    HWND window = m_ctrlThis.getWindow();
    HWND label = GetDlgItem(window, labelId);
    const RECT rect = rectIn(window, label);

    TCHAR buffer[2048] = { 0 };
    GetWindowText(label, buffer, sizeof(buffer) / sizeof(buffer[0]));
    HDC dc = GetDC(label);
    if (dc == 0) {
      return;
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessage(label, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != 0 ? SelectObject(dc, font) : 0;
    RECT measured = { 0, 0, rect.right - rect.left, 0 };
    DrawText(dc, buffer, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (oldFont != 0) {
      SelectObject(dc, oldFont);
    }
    ReleaseDC(label, dc);

    const int delta = (measured.bottom - measured.top) - (rect.bottom - rect.top);
    if (delta == 0) {
      return;
    }
    SetWindowPos(label, 0, 0, 0, rect.right - rect.left, rect.bottom - rect.top + delta,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (HWND child = GetWindow(window, GW_CHILD); child != 0;
         child = GetWindow(child, GW_HWNDNEXT)) {
      const RECT childRect = rectIn(window, child);
      if (child != label && childRect.top >= rect.bottom) {
        SetWindowPos(child, 0, childRect.left, childRect.top + delta, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
    RECT windowRect;
    GetWindowRect(window, &windowRect);
    SetWindowPos(window, 0, 0, 0, windowRect.right - windowRect.left,
                 windowRect.bottom - windowRect.top + delta,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window, 0, TRUE);
  }

  static void moveReconnectBeforeClose(HWND window)
  {
    HWND reconnect = GetDlgItem(window, ReconnectDialog::ID_RECONNECT);
    const RECT reconnectRect = rectIn(window, reconnect);
    const RECT stopRect = rectIn(window, GetDlgItem(window, ReconnectDialog::ID_STOP));
    const RECT closeRect = rectIn(window, GetDlgItem(window, ReconnectDialog::ID_CLOSE));
    const int gap = closeRect.left - stopRect.right;
    const int left = closeRect.left - gap - (reconnectRect.right - reconnectRect.left);
    SetWindowPos(reconnect, 0, left, reconnectRect.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  static void centerOnOwner(HWND window)
  {
    HWND owner = GetWindow(window, GW_OWNER);
    RECT dialog, area;
    if (owner == 0 || !GetWindowRect(window, &dialog) || !GetWindowRect(owner, &area)) {
      return;
    }
    const int width = dialog.right - dialog.left;
    const int height = dialog.bottom - dialog.top;
    int x = area.left + ((area.right - area.left) - width) / 2;
    int y = area.top + ((area.bottom - area.top) - height) / 2;
    MONITORINFO monitor;
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfo(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
      const RECT &work = monitor.rcWork;
      x = std::max<int>(work.left, std::min<int>(x, work.right - width));
      y = std::max<int>(work.top, std::min<int>(y, work.bottom - height));
    }
    SetWindowPos(window, 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  Text m_caption;
  Text m_instruction;
  Text m_reason;
  unsigned int m_delayMs;
  unsigned int m_nextAttempt;
  unsigned int m_maxAttempts;
  bool m_autoEnabled;
  bool m_clicked;
  bool m_centerOnOwner;
  DWORD m_startTick;
  unsigned int m_shownSeconds;
  DialogTheme m_theme;
};

}

bool ReconnectDialog::show(HWND owner, const TCHAR *caption, const TCHAR *instruction,
                           const TCHAR *reason, unsigned int autoDelayMs,
                           unsigned int nextAttempt, unsigned int maxAttempts)
{
  ReconnectPrompt prompt(caption, instruction, reason, autoDelayMs, nextAttempt, maxAttempts,
                         owner != 0);
  Control parent(owner);
  if (owner != 0) {
    prompt.setParent(&parent);
  }
  const int button = prompt.showModal();
  if (button > 0) {
    return button == ID_RECONNECT;
  }
  // Without the dialog, a plain message box offers a manual reconnect.
  Text text = Text(instruction != 0 ? instruction : _T("")) + _T("\n\n") +
              (reason != 0 ? reason : _T(""));
  return MessageBox(owner, text.c_str(), caption != 0 ? caption : _T("FarSightVNC"),
                    MB_RETRYCANCEL | MB_ICONWARNING) == IDRETRY;
}
