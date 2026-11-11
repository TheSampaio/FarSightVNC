// Copyright (C) 2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
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
//-------------------------------------------------------------------------
//
// Modified for FarSightVNC on 2026-11-03: Options dialog split into Display,
// Input and Network tab pages, with Ctrl+Tab switching between them.
// Modified for FarSightVNC on 2026-11-11: the scale control shows "Fit to window", disabled (the remote screen always fits).
//

#include "OptionsDialog.h"

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

OptionsPage::OptionsPage(DWORD resourceId, OptionsDialog *owner)
: BaseDialog(resourceId),
  m_owner(owner)
{
}

BOOL OptionsPage::onInitDialog()
{
  // Paint the page background like the tab body.
  EnableThemeDialogTexture(m_ctrlThis.getWindow(), ETDT_ENABLETAB);
  return FALSE;
}

BOOL OptionsPage::onCommand(UINT controlID, UINT notificationID)
{
  return m_owner->onControlCommand(controlID, notificationID);
}

void OptionsPage::onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  m_owner->onMessageReceived(uMsg, wParam, lParam);
}

OptionsDialog *OptionsDialog::s_activeDialog = NULL;

OptionsDialog::OptionsDialog()
: BaseDialog(IDD_OPTIONS),
  m_connected(false),
  m_displayPage(IDD_OPTIONS_DISPLAY, this),
  m_inputPage(IDD_OPTIONS_INPUT, this),
  m_networkPage(IDD_OPTIONS_NETWORK, this),
  m_msgHook(NULL),
  m_previousDialog(NULL)
{
}

void OptionsDialog::setConnectionConfig(ConnectionConfig *conConfig)
{
  m_conConfig = conConfig;
}

void OptionsDialog::setConnected()
{
  m_connected = true;
}

BOOL OptionsDialog::onCommand(UINT controlID, UINT notificationID)
{
  if (controlID == IDOK) {
    if (onOkPressed()) {
      kill(1);
    }
    return TRUE;
  }
  if (controlID == IDCANCEL) {
    kill(0);
    return TRUE;
  }
  return FALSE;
}

BOOL OptionsDialog::onControlCommand(UINT controlID, UINT notificationID)
{
  if (controlID == IDC_CVIEWONLY) {
    if (notificationID == BN_CLICKED) {
      onViewOnlyClick();
      return TRUE;
    }
  }
  if (controlID == IDC_CEIGHTBIT) {
    if (notificationID == BN_CLICKED) {
      on8BitColorClick();
      return TRUE;
    }
  }
  if (controlID == IDC_CJPEG) {
    if (notificationID == BN_CLICKED) {
      onAllowJpegCompressionClick();
      return TRUE;
    }
  }
  if (controlID == IDC_CCOMPRLVL) {
    if (notificationID == BN_CLICKED) {
      onAllowCustomCompressionClick();
      return TRUE;
    }
  }
  if (controlID == IDC_CUSEENC) {
    if (notificationID == CBN_SELCHANGE) {
      onPreferredEncodingSelectionChange();
      return TRUE;
    }
  }
  return FALSE;
}

BOOL OptionsDialog::onNotify(UINT controlID, LPARAM data)
{
  if (controlID == IDC_OPTIONS_TAB) {
    Tab *tab = m_tabControl.getTab(m_tabControl.getSelectedTabIndex());
    switch (reinterpret_cast<LPNMHDR>(data)->code) {
    case TCN_SELCHANGING:
      tab->setVisible(false);
      break;
    case TCN_SELCHANGE:
      tab->setVisible(true);
      break;
    }
  }
  return FALSE;
}

void OptionsDialog::createPage(OptionsPage *page, const TCHAR *caption)
{
  page->setParent(&m_ctrlThis);
  page->create();

  // Fit the page into the display area of the tab control.
  RECT rect;
  m_tabControl.adjustRect(&rect);
  MapWindowPoints(m_tabControl.getWindow(), m_ctrlThis.getWindow(),
                  reinterpret_cast<POINT *>(&rect), 2);
  MoveWindow(page->getControl()->getWindow(), rect.left, rect.top,
             rect.right - rect.left, rect.bottom - rect.top, TRUE);

  // Put the page right after the tab control in the Z order, so that Tab
  // goes from the tab control to the page controls and then to OK and Cancel.
  SetWindowPos(page->getControl()->getWindow(), m_tabControl.getWindow(),
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

  m_tabControl.addTab(page, caption);
}

HWND OptionsDialog::findControl(UINT controlID)
{
  OptionsPage *pages[] = {&m_displayPage, &m_inputPage, &m_networkPage};
  for (int i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
    HWND control = GetDlgItem(pages[i]->getControl()->getWindow(), controlID);
    if (control != NULL) {
      return control;
    }
  }
  return NULL;
}

void OptionsDialog::setPageControl(Control &control, DWORD controlID)
{
  control = findControl(controlID);
}

// Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PgDn and Ctrl+PgUp switch the tab. A dialog
// does this only for property sheets, so it is done here.
bool OptionsDialog::onTabSwitchKey(const MSG *msg)
{
  HWND dialog = m_ctrlThis.getWindow();
  if (msg->hwnd != dialog && !IsChild(dialog, msg->hwnd)) {
    return false;
  }
  if (GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_MENU) < 0) {
    return false;
  }
  int step;
  if (msg->wParam == VK_TAB) {
    step = (GetKeyState(VK_SHIFT) < 0) ? -1 : 1;
  } else if (msg->wParam == VK_NEXT) {
    step = 1;
  } else if (msg->wParam == VK_PRIOR) {
    step = -1;
  } else {
    return false;
  }

  int count = static_cast<int>(m_tabControl.getTabs().size());
  int index = (m_tabControl.getSelectedTabIndex() + step + count) % count;
  bool tabHadFocus = (GetFocus() == m_tabControl.getWindow());
  m_tabControl.showTab(index);
  if (!tabHadFocus) {
    HWND page = m_tabControl.getTab(index)->getDialog()->getControl()->getWindow();
    HWND first = GetNextDlgTabItem(page, NULL, FALSE);
    if (first != NULL) {
      SetFocus(first);
    }
  }
  return true;
}

LRESULT CALLBACK OptionsDialog::msgFilterProc(int code, WPARAM wParam, LPARAM lParam)
{
  if (code == MSGF_DIALOGBOX) {
    const MSG *msg = reinterpret_cast<const MSG *>(lParam);
    if (msg->message == WM_KEYDOWN) {
      for (OptionsDialog *dialog = s_activeDialog; dialog != NULL;
           dialog = dialog->m_previousDialog) {
        if (dialog->onTabSwitchKey(msg)) {
          return 1;
        }
      }
    }
  }
  return CallNextHookEx(NULL, code, wParam, lParam);
}

BOOL OptionsDialog::onDestroy()
{
  if (m_msgHook != NULL) {
    UnhookWindowsHookEx(m_msgHook);
    m_msgHook = NULL;
    for (OptionsDialog **link = &s_activeDialog; *link != NULL;
         link = &(*link)->m_previousDialog) {
      if (*link == this) {
        *link = m_previousDialog;
        break;
      }
    }
    m_previousDialog = NULL;
  }
  m_tabControl.deleteAllTabs();
  return TRUE;
}

BOOL OptionsDialog::onInitDialog()
{
  m_tabControl.setWindow(GetDlgItem(m_ctrlThis.getWindow(), IDC_OPTIONS_TAB));
  createPage(&m_displayPage, StringTable::getString(IDS_OPTIONS_TAB_DISPLAY));
  createPage(&m_inputPage, StringTable::getString(IDS_OPTIONS_TAB_INPUT));
  createPage(&m_networkPage, StringTable::getString(IDS_OPTIONS_TAB_NETWORK));
  m_tabControl.showTab(0);

  if (m_msgHook == NULL) {
    m_msgHook = SetWindowsHookEx(WH_MSGFILTER, msgFilterProc, NULL,
                                 GetCurrentThreadId());
    if (m_msgHook != NULL) {
      m_previousDialog = s_activeDialog;
      s_activeDialog = this;
    }
  }

  setPageControl(m_useEnc, IDC_CUSEENC);
  setPageControl(m_eightBit, IDC_CEIGHTBIT);
  setPageControl(m_compLvl, IDC_CCOMPRLVL);
  setPageControl(m_tcompLvl, IDC_SCOMP);
  setPageControl(m_quality, IDC_SQUALITY);
  setPageControl(m_jpeg, IDC_CJPEG);
  setPageControl(m_tjpeg, IDC_SJPEG);
  setPageControl(m_quality2, IDC_SQUALITY2);
  setPageControl(m_copyrect, IDC_CCOPYRECT);
  setPageControl(m_viewonly, IDC_CVIEWONLY);
  setPageControl(m_disclip, IDC_CDISCLIP);
  setPageControl(m_sharedses, IDC_CSHAREDSES);
  setPageControl(m_scale, IDC_CSCALE);
  setPageControl(m_fullscr, IDC_CFULLSCR);
  setPageControl(m_deiconfy, IDC_CDEICONFY);
  setPageControl(m_swapmouse, IDC_CSWAPMOUSE);
  setPageControl(m_track, IDC_RTRACK);
  setPageControl(m_cursor, IDC_RCURSOR);
  setPageControl(m_ncursor, IDC_RNCURSOR);
  setPageControl(m_dot, IDC_RDOT);
  setPageControl(m_smalldot, IDC_RSMALLDOT);
  setPageControl(m_arrow, IDC_RARROW);
  setPageControl(m_nlocal, IDC_RNLOCAL);

  m_useEnc.addItem(_T("Raw"), reinterpret_cast<void *>(EncodingDefs::RAW));
  m_useEnc.addItem(_T("Hextile"), reinterpret_cast<void *>(EncodingDefs::HEXTILE));
  m_useEnc.addItem(_T("Tight"), reinterpret_cast<void *>(EncodingDefs::TIGHT));
  m_useEnc.addItem(_T("RRE"), reinterpret_cast<void *>(EncodingDefs::RRE));
  m_useEnc.addItem(_T("ZRLE"), reinterpret_cast<void *>(EncodingDefs::ZRLE));


  // The remote screen always fits the window: the scale is not a choice.
  m_scale.addItem(_T("Fit to window"));
  m_scale.setEnabled(false);

  m_tjpeg.setRange(0, 9);
  m_tcompLvl.setRange(0, 9);
  updateControlValues();
  m_tabControl.setFocus();
  return FALSE;
}

void OptionsDialog::updateControlValues()
{
  // Preferred encoding
  for (int i = 0; i < m_useEnc.getItemsCount(); i++) {
    int enc = reinterpret_cast<int>(m_useEnc.getItemData(i));
    if (enc == m_conConfig->getPreferredEncoding()) {
      m_useEnc.setSelectedItem(i);
      break;
    } // if found

    // set default value, if preferred encoding not in list
    if (enc == EncodingDefs::HEXTILE)
      m_useEnc.setSelectedItem(i);
  } // for i

  m_eightBit.check(m_conConfig->isUsing8BitColor());

  m_compLvl.check(m_conConfig->isCustomCompressionEnabled());
  m_jpeg.check(m_conConfig->isJpegCompressionEnabled());

  m_copyrect.check(m_conConfig->isCopyRectAllowed());
  m_viewonly.check(m_conConfig->isViewOnly());
  m_disclip.check(!m_conConfig->isClipboardEnabled());
  m_fullscr.check(m_conConfig->isFullscreenEnabled());
  m_deiconfy.check(m_conConfig->isDeiconifyOnRemoteBellEnabled());
  m_swapmouse.check(m_conConfig->isMouseSwapEnabled());

  m_sharedses.check(m_conConfig->getSharedFlag());
  m_sharedses.setEnabled(!m_connected);

  m_scale.setSelectedItem(0);

  {
    bool enableCursorUpdate = m_conConfig->isRequestingShapeUpdates();
    bool ignoreCursorUpdate = m_conConfig->isIgnoringShapeUpdates();
    m_track.check(enableCursorUpdate && !ignoreCursorUpdate);
    m_cursor.check(!enableCursorUpdate && ignoreCursorUpdate);
    m_ncursor.check(enableCursorUpdate && ignoreCursorUpdate);
  }

  StringStorage labelText;
  {
    const int DEFAULT_COMPRESSION_LEVEL = 6;
    int level = DEFAULT_COMPRESSION_LEVEL;
    if (m_conConfig->isCustomCompressionEnabled())
      level = m_conConfig->getCustomCompressionLevel();
    m_tcompLvl.setPos(level);
    labelText.format(_T("%d"), level);
    m_quality.setText(labelText.getString());
  }

  {
  const int DEFAULT_JPEG_COMPRESSION_LEVEL = 6;
    int level = DEFAULT_JPEG_COMPRESSION_LEVEL;
    if (m_conConfig->isJpegCompressionEnabled())
      level = m_conConfig->getJpegCompressionLevel();
    m_tjpeg.setPos(level);
    labelText.format(_T("%d"), level);
    m_quality2.setText(labelText.getString());
  }

  switch (m_conConfig->getLocalCursorShape()) {
  case ConnectionConfig::SMALL_CURSOR:
    m_smalldot.check(true);
    break;
  case ConnectionConfig::NORMAL_CURSOR:
    m_arrow.check(true);
    break;
  case ConnectionConfig::NO_CURSOR:
    m_nlocal.check(true);
    break;
  default:
    m_dot.check(true);
    break;
  }

  onViewOnlyClick();

  onAllowCustomCompressionClick();
  onAllowJpegCompressionClick();

  onCustomCompressionLevelScroll();
  onJpegCompressionLevelScroll();

  onPreferredEncodingSelectionChange();
  on8BitColorClick();
}

void OptionsDialog::onViewOnlyClick()
{
  if (m_viewonly.isChecked()) {
    m_swapmouse.setEnabled(false);
  } else {
    m_swapmouse.setEnabled(true);
  }
}

void OptionsDialog::on8BitColorClick()
{
  if (!m_eightBit.isChecked()) {
    if (m_jpeg.isChecked()) {
      enableJpegCompression(true);
    }
    m_jpeg.setEnabled(true);
  } else {
    m_jpeg.setEnabled(false);
    enableJpegCompression(false);
  }
}

void OptionsDialog::enableJpegCompression(bool enable)
{
  m_tjpeg.setEnabled(enable);
  m_quality2.setEnabled(enable);
  EnableWindow(findControl(IDC_SPOOR), enable);
  EnableWindow(findControl(IDC_SBEST2), enable);
  EnableWindow(findControl(IDC_STQUALITY2), enable);
}
void OptionsDialog::onAllowCustomCompressionClick()
{
  enableCustomCompression(m_compLvl.isChecked());
}

void OptionsDialog::enableCustomCompression(bool enable)
{
  m_tcompLvl.setEnabled(enable);
  m_quality.setEnabled(enable);
  EnableWindow(findControl(IDC_SBEST), enable);
  EnableWindow(findControl(IDC_SFAST), enable);
  EnableWindow(findControl(IDC_STQUALITY), enable);
}

void OptionsDialog::onAllowJpegCompressionClick()
{
  enableJpegCompression(m_jpeg.isChecked());
}

void OptionsDialog::onPreferredEncodingSelectionChange()
{
  int index = m_useEnc.getSelectedItemIndex();
  if (index < 0) {
    return ;
  }
  int encoding = reinterpret_cast<int>(m_useEnc.getItemData(index));
  switch (encoding) {
  case EncodingDefs::TIGHT:
    enableCustomCompression(m_compLvl.isChecked());
    m_compLvl.setEnabled(true);
    break;
  default:
    enableCustomCompression(false);
    m_compLvl.setEnabled(false);
    break;
  } // switch
} // void

void OptionsDialog::onCustomCompressionLevelScroll()
{
  StringStorage labelText;
  labelText.format(_T("%ld"), m_tcompLvl.getPos());
  m_quality.setText(labelText.getString());
}

void OptionsDialog::onJpegCompressionLevelScroll()
{
  StringStorage labelText;
  labelText.format(_T("%ld"), m_tjpeg.getPos());
  m_quality2.setText(labelText.getString());
}

void OptionsDialog::onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {
  case WM_HSCROLL:
    if (HWND(lParam) == m_tcompLvl.getWindow()) {
      onCustomCompressionLevelScroll();
    }
    if (HWND(lParam) == m_tjpeg.getWindow()) {
      onJpegCompressionLevelScroll();
    }
    break;
  }
}

bool OptionsDialog::onOkPressed()
{
  apply();
  return true;
}

void OptionsDialog::apply()
{
  // Preferred encoding
  int pesii = m_useEnc.getSelectedItemIndex();
  if (pesii >= 0) {
    int preferredEncoding = reinterpret_cast<int>(m_useEnc.getItemData(pesii));
    m_conConfig->setPreferredEncoding(preferredEncoding);
  } else {
    _ASSERT(pesii >= 0);
    m_conConfig->setPreferredEncoding(EncodingDefs::TIGHT);
  }

  if (m_compLvl.isChecked()) {
    int level = static_cast<int>(m_tcompLvl.getPos());
    m_conConfig->setCustomCompressionLevel(level);
  } else {
    m_conConfig->disableCustomCompression();
  }

  if (m_jpeg.isChecked()) {
    int level = static_cast<int>(m_tjpeg.getPos());
    m_conConfig->setJpegCompressionLevel(level);
  } else {
    m_conConfig->disableJpegCompression();
  }

  m_conConfig->use8BitColor(m_eightBit.isChecked());
  m_conConfig->allowCopyRect(m_copyrect.isChecked());
  m_conConfig->setViewOnly(m_viewonly.isChecked());
  m_conConfig->enableClipboard(!m_disclip.isChecked());
  m_conConfig->enableFullscreen(m_fullscr.isChecked());
  m_conConfig->deiconifyOnRemoteBell(m_deiconfy.isChecked());
  m_conConfig->swapMouse(m_swapmouse.isChecked());
  m_conConfig->setSharedFlag(m_sharedses.isChecked());

  m_conConfig->fitWindow(true);

  if (m_track.isChecked()) {
    m_conConfig->requestShapeUpdates(true);
    m_conConfig->ignoreShapeUpdates(false);
  }

  if (m_cursor.isChecked()) {
    m_conConfig->requestShapeUpdates(false);
    m_conConfig->ignoreShapeUpdates(true);
  }

  if (m_ncursor.isChecked()) {
    m_conConfig->requestShapeUpdates(true);
    m_conConfig->ignoreShapeUpdates(true);
  }

  int localCursorShape = ConnectionConfig::DOT_CURSOR;
  if (m_smalldot.isChecked()) {
    localCursorShape = ConnectionConfig::SMALL_CURSOR;
  } else if (m_arrow.isChecked()) {
    localCursorShape = ConnectionConfig::NORMAL_CURSOR;
  } else if (m_nlocal.isChecked()) {
    localCursorShape = ConnectionConfig::NO_CURSOR;
  }

  m_conConfig->setLocalCursorShape(localCursorShape);
}
