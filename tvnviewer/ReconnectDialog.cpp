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

// The task dialog API is declared for Windows Vista and later only; this file
// includes no project header that would lower the version again. The viewer
// manifest selects Common Controls 6, which implements it.
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0600
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string>

#include "ReconnectDialog.h"
#include "ReconnectPolicy.h"

namespace {

std::wstring toWide(const TCHAR *text)
{
  if (text == 0) {
    return std::wstring();
  }
#ifdef UNICODE
  return std::wstring(text);
#else
  int length = MultiByteToWideChar(CP_ACP, 0, text, -1, 0, 0);
  if (length <= 1) {
    return std::wstring();
  }
  std::wstring result(length - 1, L'\0');
  MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], length);
  return result;
#endif
}

struct PromptState
{
  std::wstring reason;
  std::wstring content;
  unsigned int delayMs;
  unsigned int nextAttempt;
  unsigned int maxAttempts;
  bool autoEnabled;
  bool clicked;
  DWORD startTick;
  unsigned int shownSeconds;
};

std::wstring buildContent(const PromptState *state, unsigned int secondsLeft)
{
  std::wstring content = state->reason;
  if (state->delayMs == 0) {
    return content;
  }
  wchar_t line[160];
  if (state->autoEnabled) {
    swprintf_s(line, L"Reconnecting automatically in %u s (attempt %u of %u).",
               secondsLeft, state->nextAttempt, state->maxAttempts);
  } else {
    swprintf_s(line, L"Automatic reconnection stopped.");
  }
  if (!content.empty()) {
    content += L"\n\n";
  }
  content += line;
  return content;
}

void refreshContent(HWND hwnd, PromptState *state, unsigned int secondsLeft)
{
  state->shownSeconds = secondsLeft;
  state->content = buildContent(state, secondsLeft);
  SendMessage(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
              reinterpret_cast<LPARAM>(state->content.c_str()));
}

HRESULT CALLBACK promptCallback(HWND hwnd, UINT notification, WPARAM wParam,
                                LPARAM lParam, LONG_PTR data)
{
  PromptState *state = reinterpret_cast<PromptState *>(data);
  switch (notification) {
  case TDN_CREATED:
    state->startTick = GetTickCount();
    break;
  case TDN_BUTTON_CLICKED:
    if (wParam == ReconnectDialog::ID_STOP) {
      // Stops the countdown; the prompt stays open.
      state->autoEnabled = false;
      refreshContent(hwnd, state, 0);
      SendMessage(hwnd, TDM_ENABLE_BUTTON, ReconnectDialog::ID_STOP, FALSE);
      return S_FALSE;
    }
    break;
  case TDN_TIMER:
    if (state->autoEnabled && !state->clicked && state->delayMs != 0) {
      unsigned int left = ReconnectPolicy::secondsLeft(state->delayMs,
                                                       GetTickCount() - state->startTick);
      if (left == 0) {
        state->clicked = true;
        PostMessage(hwnd, TDM_CLICK_BUTTON, ReconnectDialog::ID_RECONNECT, 0);
      } else if (left != state->shownSeconds) {
        refreshContent(hwnd, state, left);
      }
    }
    break;
  }
  return S_OK;
}

} // namespace

bool ReconnectDialog::show(HWND owner, const TCHAR *caption, const TCHAR *instruction,
                           const TCHAR *reason, unsigned int autoDelayMs,
                           unsigned int nextAttempt, unsigned int maxAttempts)
{
  std::wstring captionText = toWide(caption);
  std::wstring instructionText = toWide(instruction);

  PromptState state;
  state.reason = toWide(reason);
  state.delayMs = autoDelayMs;
  state.nextAttempt = nextAttempt;
  state.maxAttempts = maxAttempts;
  state.autoEnabled = autoDelayMs != 0;
  state.clicked = false;
  state.startTick = GetTickCount();
  state.shownSeconds = ReconnectPolicy::secondsLeft(autoDelayMs, 0);
  state.content = buildContent(&state, state.shownSeconds);

  const TASKDIALOG_BUTTON buttons[] = {
    { ID_RECONNECT, L"&Reconnect" },
    { ID_STOP, L"&Stop retrying" },
    { ID_CLOSE, L"&Close" },
  };

  TASKDIALOGCONFIG config;
  ZeroMemory(&config, sizeof(config));
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_CALLBACK_TIMER;
  if (owner != 0) {
    config.dwFlags |= TDF_POSITION_RELATIVE_TO_WINDOW;
  }
  config.pszWindowTitle = captionText.c_str();
  config.pszMainIcon = TD_WARNING_ICON;
  config.pszMainInstruction = instructionText.c_str();
  config.pszContent = state.content.c_str();
  TASKDIALOG_BUTTON noStopButtons[2];
  // Without a countdown there is nothing to stop.
  const UINT buttonCount = sizeof(buttons) / sizeof(buttons[0]);
  if (autoDelayMs != 0) {
    config.cButtons = buttonCount;
    config.pButtons = buttons;
  } else {
    noStopButtons[0] = buttons[0];
    noStopButtons[1] = buttons[2];
    config.cButtons = 2;
    config.pButtons = noStopButtons;
  }
  config.nDefaultButton = ID_RECONNECT;
  config.pfCallback = promptCallback;
  config.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);

  int button = 0;
  if (SUCCEEDED(TaskDialogIndirect(&config, &button, 0, 0))) {
    return button == ID_RECONNECT;
  }
  // Without the task dialog, a plain message box offers a manual reconnect.
  std::wstring text = instructionText + L"\n\n" + state.reason;
  return MessageBoxW(owner, text.c_str(), captionText.c_str(),
                     MB_RETRYCANCEL | MB_ICONWARNING) == IDRETRY;
}
