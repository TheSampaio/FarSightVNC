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
// The option logic comes from OptionsDialog.cpp (Copyright (C) 2012 GlavSoft
// LLC), moved here unchanged so that the Options dialog and the Settings page
// share it.
//

#include "ConnectionOptionsForm.h"

#include "rfb/EncodingDefs.h"
#include "util/StringStorage.h"
#include "resource.h"

namespace {

bool exists(Control &control)
{
  return control.getWindow() != 0;
}

}

ConnectionOptionsForm::ConnectionOptionsForm()
{
}

HWND ConnectionOptionsForm::findControl(UINT controlID) const
{
  return m_finder ? m_finder(controlID) : 0;
}

void ConnectionOptionsForm::enableControl(UINT controlID, bool enable)
{
  HWND control = findControl(controlID);
  if (control != 0) {
    EnableWindow(control, enable);
  }
}

void ConnectionOptionsForm::bind(const ControlFinder &finder)
{
  m_finder = finder;

  m_useEnc.setWindow(findControl(IDC_CUSEENC));
  m_eightBit.setWindow(findControl(IDC_CEIGHTBIT));
  m_compLvl.setWindow(findControl(IDC_CCOMPRLVL));
  m_tcompLvl.setWindow(findControl(IDC_SCOMP));
  m_quality.setWindow(findControl(IDC_SQUALITY));
  m_jpeg.setWindow(findControl(IDC_CJPEG));
  m_tjpeg.setWindow(findControl(IDC_SJPEG));
  m_quality2.setWindow(findControl(IDC_SQUALITY2));
  m_copyrect.setWindow(findControl(IDC_CCOPYRECT));
  m_viewonly.setWindow(findControl(IDC_CVIEWONLY));
  m_disclip.setWindow(findControl(IDC_CDISCLIP));
  m_sharedses.setWindow(findControl(IDC_CSHAREDSES));
  m_scale.setWindow(findControl(IDC_CSCALE));
  m_fullscr.setWindow(findControl(IDC_CFULLSCR));
  m_deiconfy.setWindow(findControl(IDC_CDEICONFY));
  m_swapmouse.setWindow(findControl(IDC_CSWAPMOUSE));
  m_track.setWindow(findControl(IDC_RTRACK));
  m_cursor.setWindow(findControl(IDC_RCURSOR));
  m_ncursor.setWindow(findControl(IDC_RNCURSOR));
  m_dot.setWindow(findControl(IDC_RDOT));
  m_smalldot.setWindow(findControl(IDC_RSMALLDOT));
  m_arrow.setWindow(findControl(IDC_RARROW));
  m_nlocal.setWindow(findControl(IDC_RNLOCAL));

  if (exists(m_useEnc)) {
    m_useEnc.addItem(_T("Raw"), reinterpret_cast<void *>(static_cast<INT_PTR>(EncodingDefs::RAW)));
    m_useEnc.addItem(_T("Hextile"), reinterpret_cast<void *>(static_cast<INT_PTR>(EncodingDefs::HEXTILE)));
    m_useEnc.addItem(_T("Tight"), reinterpret_cast<void *>(static_cast<INT_PTR>(EncodingDefs::TIGHT)));
    m_useEnc.addItem(_T("RRE"), reinterpret_cast<void *>(static_cast<INT_PTR>(EncodingDefs::RRE)));
    m_useEnc.addItem(_T("ZRLE"), reinterpret_cast<void *>(static_cast<INT_PTR>(EncodingDefs::ZRLE)));
  }

  // The remote screen always fits the window: the scale is not a choice.
  if (exists(m_scale)) {
    m_scale.addItem(_T("Fit to window"));
    m_scale.setEnabled(false);
  }

  if (exists(m_tjpeg)) {
    m_tjpeg.setRange(0, 9);
  }
  if (exists(m_tcompLvl)) {
    m_tcompLvl.setRange(0, 9);
  }
}

void ConnectionOptionsForm::load(const ConnectionConfig *constConfig, bool connected)
{
  // The getters of ConnectionConfig lock it and are not const.
  ConnectionConfig *config = const_cast<ConnectionConfig *>(constConfig);

  // Preferred encoding
  for (int i = 0; i < m_useEnc.getItemsCount(); i++) {
    int enc = static_cast<int>(reinterpret_cast<INT_PTR>(m_useEnc.getItemData(i)));
    if (enc == config->getPreferredEncoding()) {
      m_useEnc.setSelectedItem(i);
      break;
    } // if found

    // set default value, if preferred encoding not in list
    if (enc == EncodingDefs::HEXTILE)
      m_useEnc.setSelectedItem(i);
  } // for i

  m_eightBit.check(config->isUsing8BitColor());

  m_compLvl.check(config->isCustomCompressionEnabled());
  m_jpeg.check(config->isJpegCompressionEnabled());

  m_copyrect.check(config->isCopyRectAllowed());
  m_viewonly.check(config->isViewOnly());
  m_disclip.check(!config->isClipboardEnabled());
  m_fullscr.check(config->isFullscreenEnabled());
  m_deiconfy.check(config->isDeiconifyOnRemoteBellEnabled());
  m_swapmouse.check(config->isMouseSwapEnabled());

  m_sharedses.check(config->getSharedFlag());
  m_sharedses.setEnabled(!connected);

  if (exists(m_scale)) {
    m_scale.setSelectedItem(0);
  }

  {
    bool enableCursorUpdate = config->isRequestingShapeUpdates();
    bool ignoreCursorUpdate = config->isIgnoringShapeUpdates();
    m_track.check(enableCursorUpdate && !ignoreCursorUpdate);
    m_cursor.check(!enableCursorUpdate && ignoreCursorUpdate);
    m_ncursor.check(enableCursorUpdate && ignoreCursorUpdate);
  }

  StringStorage labelText;
  {
    const int DEFAULT_COMPRESSION_LEVEL = 6;
    int level = DEFAULT_COMPRESSION_LEVEL;
    if (config->isCustomCompressionEnabled())
      level = config->getCustomCompressionLevel();
    m_tcompLvl.setPos(level);
    labelText.format(_T("%d"), level);
    m_quality.setText(labelText.getString());
  }

  {
    const int DEFAULT_JPEG_COMPRESSION_LEVEL = 6;
    int level = DEFAULT_JPEG_COMPRESSION_LEVEL;
    if (config->isJpegCompressionEnabled())
      level = config->getJpegCompressionLevel();
    m_tjpeg.setPos(level);
    labelText.format(_T("%d"), level);
    m_quality2.setText(labelText.getString());
  }

  m_dot.check(false);
  m_smalldot.check(false);
  m_arrow.check(false);
  m_nlocal.check(false);
  switch (config->getLocalCursorShape()) {
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

bool ConnectionOptionsForm::onCommand(UINT controlID, UINT notificationID)
{
  if (notificationID == BN_CLICKED) {
    switch (controlID) {
    case IDC_CVIEWONLY:
      onViewOnlyClick();
      return true;
    case IDC_CEIGHTBIT:
      on8BitColorClick();
      return true;
    case IDC_CJPEG:
      onAllowJpegCompressionClick();
      return true;
    case IDC_CCOMPRLVL:
      onAllowCustomCompressionClick();
      return true;
    case IDC_CCOPYRECT:
    case IDC_CDISCLIP:
    case IDC_CSHAREDSES:
    case IDC_CFULLSCR:
    case IDC_CDEICONFY:
    case IDC_CSWAPMOUSE:
    case IDC_RTRACK:
    case IDC_RCURSOR:
    case IDC_RNCURSOR:
    case IDC_RDOT:
    case IDC_RSMALLDOT:
    case IDC_RARROW:
    case IDC_RNLOCAL:
      return true;
    }
  }
  if (controlID == IDC_CUSEENC && notificationID == CBN_SELCHANGE) {
    onPreferredEncodingSelectionChange();
    return true;
  }
  return false;
}

bool ConnectionOptionsForm::onHScroll(HWND control)
{
  if (control == 0) {
    return false;
  }
  if (control == m_tcompLvl.getWindow()) {
    onCustomCompressionLevelScroll();
    return true;
  }
  if (control == m_tjpeg.getWindow()) {
    onJpegCompressionLevelScroll();
    return true;
  }
  return false;
}

void ConnectionOptionsForm::onViewOnlyClick()
{
  if (m_viewonly.isChecked()) {
    m_swapmouse.setEnabled(false);
  } else {
    m_swapmouse.setEnabled(true);
  }
}

void ConnectionOptionsForm::on8BitColorClick()
{
  if (!exists(m_eightBit)) {
    return;
  }
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

void ConnectionOptionsForm::enableJpegCompression(bool enable)
{
  m_tjpeg.setEnabled(enable);
  m_quality2.setEnabled(enable);
  enableControl(IDC_SPOOR, enable);
  enableControl(IDC_SBEST2, enable);
  enableControl(IDC_STQUALITY2, enable);
}

void ConnectionOptionsForm::onAllowCustomCompressionClick()
{
  enableCustomCompression(m_compLvl.isChecked());
}

void ConnectionOptionsForm::enableCustomCompression(bool enable)
{
  m_tcompLvl.setEnabled(enable);
  m_quality.setEnabled(enable);
  enableControl(IDC_SBEST, enable);
  enableControl(IDC_SFAST, enable);
  enableControl(IDC_STQUALITY, enable);
}

void ConnectionOptionsForm::onAllowJpegCompressionClick()
{
  enableJpegCompression(m_jpeg.isChecked());
}

void ConnectionOptionsForm::onPreferredEncodingSelectionChange()
{
  int index = m_useEnc.getSelectedItemIndex();
  if (index < 0) {
    return ;
  }
  int encoding = static_cast<int>(reinterpret_cast<INT_PTR>(m_useEnc.getItemData(index)));
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

void ConnectionOptionsForm::onCustomCompressionLevelScroll()
{
  StringStorage labelText;
  labelText.format(_T("%ld"), m_tcompLvl.getPos());
  m_quality.setText(labelText.getString());
}

void ConnectionOptionsForm::onJpegCompressionLevelScroll()
{
  StringStorage labelText;
  labelText.format(_T("%ld"), m_tjpeg.getPos());
  m_quality2.setText(labelText.getString());
}

void ConnectionOptionsForm::save(ConnectionConfig *config)
{
  // Only the options that have a control in the form are written: a form
  // with some of the controls (a section of the Settings page) leaves the
  // other options of config as they are.
  if (exists(m_useEnc)) {
    // Preferred encoding
    int pesii = m_useEnc.getSelectedItemIndex();
    if (pesii >= 0) {
      int preferredEncoding = static_cast<int>(reinterpret_cast<INT_PTR>(m_useEnc.getItemData(pesii)));
      config->setPreferredEncoding(preferredEncoding);
    } else {
      _ASSERT(pesii >= 0);
      config->setPreferredEncoding(EncodingDefs::TIGHT);
    }
  }

  if (exists(m_compLvl)) {
    if (m_compLvl.isChecked()) {
      int level = static_cast<int>(m_tcompLvl.getPos());
      config->setCustomCompressionLevel(level);
    } else {
      config->disableCustomCompression();
    }
  }

  if (exists(m_jpeg)) {
    if (m_jpeg.isChecked()) {
      int level = static_cast<int>(m_tjpeg.getPos());
      config->setJpegCompressionLevel(level);
    } else {
      config->disableJpegCompression();
    }
  }

  if (exists(m_eightBit)) {
    config->use8BitColor(m_eightBit.isChecked());
  }
  if (exists(m_copyrect)) {
    config->allowCopyRect(m_copyrect.isChecked());
  }
  if (exists(m_viewonly)) {
    config->setViewOnly(m_viewonly.isChecked());
  }
  if (exists(m_disclip)) {
    config->enableClipboard(!m_disclip.isChecked());
  }
  if (exists(m_fullscr)) {
    config->enableFullscreen(m_fullscr.isChecked());
  }
  if (exists(m_deiconfy)) {
    config->deiconifyOnRemoteBell(m_deiconfy.isChecked());
  }
  if (exists(m_swapmouse)) {
    config->swapMouse(m_swapmouse.isChecked());
  }
  if (exists(m_sharedses)) {
    config->setSharedFlag(m_sharedses.isChecked());
  }

  config->fitWindow(true);

  if (m_track.isChecked()) {
    config->requestShapeUpdates(true);
    config->ignoreShapeUpdates(false);
  }

  if (m_cursor.isChecked()) {
    config->requestShapeUpdates(false);
    config->ignoreShapeUpdates(true);
  }

  if (m_ncursor.isChecked()) {
    config->requestShapeUpdates(true);
    config->ignoreShapeUpdates(true);
  }

  if (exists(m_dot)) {
    int localCursorShape = ConnectionConfig::DOT_CURSOR;
    if (m_smalldot.isChecked()) {
      localCursorShape = ConnectionConfig::SMALL_CURSOR;
    } else if (m_arrow.isChecked()) {
      localCursorShape = ConnectionConfig::NORMAL_CURSOR;
    } else if (m_nlocal.isChecked()) {
      localCursorShape = ConnectionConfig::NO_CURSOR;
    }
    config->setLocalCursorShape(localCursorShape);
  }
}
