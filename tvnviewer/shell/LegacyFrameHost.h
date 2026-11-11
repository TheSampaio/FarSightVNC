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

#ifndef _LEGACY_FRAME_HOST_H_
#define _LEGACY_FRAME_HOST_H_

#include "SessionHost.h"
#include "gui/BaseWindow.h"
#include "log-writer/LogWriter.h"
#include "region/Rect.h"
#include "win-system/SystemInformation.h"

// SessionHost of a session window that is its own top-level frame (the
// TightVNC layout): every operation acts on that window, exactly as
// ViewerWindow did before the interface existed.
class LegacyFrameHost : public SessionHost
{
public:
  LegacyFrameHost(BaseWindow *window, LogWriter *logWriter);

  virtual HWND hostHwnd() const;
  virtual HWND rootHwnd() const;
  virtual void showFrame();
  virtual void setTitle(const StringStorage *title);
  virtual void enterFullscreen(const RECT &monitorRect);
  virtual void exitFullscreen();
  virtual bool isFullscreen() const;
  virtual void requestClientSize(int width, int height);
  virtual void maximize();
  virtual void onDpiChanged(const RECT &suggested);
  virtual void onSessionEnded();

private:
  // Frame rectangle for a client area of width x height on the work area of
  // the window's monitor.
  Rect calculateDefaultSize(int width, int height);

  BaseWindow *m_window;
  LogWriter *m_logWriter;
  SystemInformation m_sysinf;
  bool m_isFullscreen;
  // Placement of the windowed frame, saved when entering full screen.
  WINDOWPLACEMENT m_workArea;
  // Optimal size of the windowed frame.
  Rect m_rcNormal;
};

#endif
