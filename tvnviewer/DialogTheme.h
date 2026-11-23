// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// The theme of a classic dialog (a DIALOGEX template run by BaseDialog): the
// palette of gui/d2d/Theme on the dialog background, its labels, fields and
// check boxes, group boxes, tab controls, disabled labels, the dark frame of a top-level window and the dark
// variant of the native controls, applied again whenever
// Theme::changedMessage() comes.
//
// The dialog keeps one DialogTheme and calls attach() from onInitDialog(),
// after it has created or replaced its controls. The DialogTheme subclasses
// the window and lets the dialog procedure see every message it does not
// answer itself; it detaches on WM_NCDESTROY.
//
// The modern font comes from the template (FONT 9, "Segoe UI"), which the
// dialog manager scales to the DPI of the monitor: dialog units follow it, so
// the layout of the template stays proportional. setHeading() derives a
// larger font from it.

#ifndef _DIALOG_THEME_H_
#define _DIALOG_THEME_H_

#include "util/CommonHeader.h"

#include <functional>
#include <vector>

class DialogTheme
{
public:
  DialogTheme();
  ~DialogTheme();

  // Starts theming dialog. A child dialog (a tab page) has its own
  // DialogTheme; the one of its top-level dialog forwards the theme changes.
  void attach(HWND dialog);
  void detach();

  // A title label: semibold, a third larger than the dialog font.
  void setHeading(UINT controlId);
  // A label in the secondary text color.
  void setMuted(UINT controlId);
  // A one pixel border around a control that draws none of its own (a
  // RichEdit). Progress bars get one without being added.
  void addBorder(UINT controlId);

  // Called after the colors were applied (at attach() and on every theme
  // change), for the controls the dialog themes itself.
  void setOnApplied(const std::function<void()> &callback);
  // Called after the window handled WM_DPICHANGED.
  void setOnDpiChanged(const std::function<void()> &callback);

  // Applies the current palette again (also after controls were added).
  void apply();

private:
  DialogTheme(const DialogTheme &);
  DialogTheme &operator=(const DialogTheme &);

  static void addId(std::vector<UINT> &ids, UINT controlId);
  bool contains(const std::vector<UINT> &ids, HWND control) const;
  void deleteBrushes();
  void updateFonts();
  void themeProgressBar(HWND bar);
  void themeGroupBox(HWND box);
  void themeTabControl(HWND tab);
  void themeLabel(HWND label);
  void paintBorders();
  LRESULT onMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK subclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR data);

  HWND m_hwnd;
  HBRUSH m_backgroundBrush;
  HBRUSH m_fieldBrush;
  HFONT m_headingFont;
  std::vector<UINT> m_headings;
  std::vector<UINT> m_muted;
  std::vector<UINT> m_borders;
  std::function<void()> m_onApplied;
  std::function<void()> m_onDpiChanged;
};

#endif
