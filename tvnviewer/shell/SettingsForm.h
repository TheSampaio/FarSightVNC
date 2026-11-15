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

#ifndef _SETTINGS_FORM_H_
#define _SETTINGS_FORM_H_

#include "gui/d2d/ChromeRenderer.h"

#include <string>
#include <vector>

class SettingsFormListener
{
public:
  virtual ~SettingsFormListener() {}
  // WM_COMMAND and WM_HSCROLL of the native controls.
  virtual void onFormCommand(UINT controlId, UINT notification, HWND control) = 0;
  virtual void onFormHScroll(HWND control) = 0;
};

// Content area of the Settings page: a scrolling column of rows. The title,
// the subtitle, the group headers, the notes and the read-only values are
// drawn with ChromeRenderer (DirectWrite); the inputs are native controls
// (check boxes, radio buttons, combo boxes, edit fields with spin buttons,
// track bars, push buttons) for keyboard and accessibility. A field has a
// native label before it, which names it for screen readers.
//
// Every size is in DIPs on an 8 DIP grid and follows the DPI of the window
// (fonts and positions are rebuilt by setDpi()). The window is a control
// parent: the dialog manager of the page moves the focus with Tab between the
// controls in creation order, and the form scrolls to the focused control.
// The mouse wheel scrolls the form, also over a control.
class SettingsForm
{
public:
  SettingsForm(HWND parent, SettingsFormListener *listener);
  ~SettingsForm();

  HWND getHWnd() const { return m_hwnd; }
  void setDpi(UINT dpi);

  // Removes every row and control and scrolls back to the top.
  void clear();
  // Title and subtitle of the section, at the top.
  void setHeading(const wchar_t *title, const wchar_t *subtitle);

  // Rows, from top to bottom. indent is a number of 24 DIP steps.
  void header(const wchar_t *text);
  void note(const wchar_t *text, int indent = 0);
  // A read-only "label    value" line.
  void value(const wchar_t *label, const wchar_t *text);
  HWND checkBox(UINT id, const wchar_t *label, int indent = 0);
  // group: first radio button of a group.
  HWND radio(UINT id, const wchar_t *label, bool group, int indent = 0);
  HWND comboBox(UINT id, const wchar_t *label, float width);
  // Edit field for a number between minimum and maximum with spin buttons.
  HWND numberField(UINT id, const wchar_t *label, int minimum, int maximum);
  // Read-only edit field (a path that can be selected and copied).
  HWND textField(UINT id, const wchar_t *label);
  // Track bar between two captions with the current value after them; the
  // caption and value IDs may be 0.
  HWND trackBar(UINT id, UINT lowId, const wchar_t *low, UINT highId, const wchar_t *high,
                UINT valueId, int indent = 0);
  // Push button on its own row, or after the field or button of the
  // previous row.
  HWND button(UINT id, const wchar_t *label, bool sameRow = false);

  // Control of the form by ID, or 0.
  HWND control(UINT id) const;
  // Lays the rows out again (after rows were added).
  void layout();

private:
  SettingsForm(const SettingsForm &);
  SettingsForm &operator=(const SettingsForm &);

  enum RowKind
  {
    ROW_HEADER,
    ROW_NOTE,
    ROW_VALUE,
    ROW_CHECK,
    ROW_FIELD,      // Native label and a control.
    ROW_TRACK,
    ROW_BUTTON
  };

  struct Row
  {
    RowKind kind;
    std::wstring text;
    std::wstring detail;  // Value of ROW_VALUE.
    int indent;
    float width;          // Width of the control, 0 for the default.
    HWND label;           // Native label (fields, track bar captions).
    HWND control;
    HWND extra;           // Spin buttons, second caption of a track bar.
    HWND value;           // Value of a track bar.
    HWND trailing;        // Push button after the control of the row.
    float top;            // Layout, form coordinates.
    float height;
  };

  static bool registerClass();
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK controlProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR id, DWORD_PTR data);
  LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);

  HWND createControl(const wchar_t *className, const wchar_t *text, DWORD style, UINT id);
  Row newRow(RowKind kind, const wchar_t *text, int indent);
  void updateFont();
  void placeRows();
  void place(HWND hwnd, float left, float top, float width, float height);
  float textWidth(const wchar_t *text) const;
  float buttonWidthOf(HWND button) const;
  float contentLeft() const;
  float contentWidth() const;
  void scrollTo(float offset);
  void ensureVisible(HWND control);
  void paint();
  // Theme::changedMessage(): the GDI brushes and the native controls follow
  // the new palette, the drawn part is painted again.
  void onThemeChanged();
  void invalidate();

  HWND m_hwnd;
  SettingsFormListener *m_listener;
  ChromeRenderer m_chrome;
  HFONT m_font;
  HBRUSH m_background;
  // Background of the fields and of the drop-down list of a combo box.
  HBRUSH m_fieldBrush;
  std::wstring m_title;
  std::wstring m_subtitle;
  float m_subtitleHeight;
  std::vector<Row> m_rows;
  float m_contentHeight;
  float m_scroll;
};

#endif
