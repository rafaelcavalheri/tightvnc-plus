// Copyright (C) 2025
// All rights reserved.
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

#include "ConnectionConfigDialog.h"
#include "ConfigDialog.h"
#include "ServerConfigDialog.h"
#include "tvnserver/resource_screenguard.h"
#include "tvnserver/resource_connectiontab.h"
#include "server-config-lib/Configurator.h"
#include "win-system/Environment.h"
#include "util/GdiplusPngLoader.h"

#include <gdiplus.h>

using namespace Gdiplus;

#pragma comment(lib, "gdiplus.lib")

namespace {

// Small helper to fill an in-memory DLGTEMPLATE byte buffer sequentially.
// See the DLGTEMPLATE layout on MSDN: a fixed-size header followed by
// WORD-aligned variable-length fields (menu, class, title, and -- because
// DS_SETFONT is set -- point size and typeface).
class TemplateWriter
{
public:
  TemplateWriter(BYTE *buffer) : m_ptr(buffer) {}

  void writeWord(WORD w)
  {
    memcpy(m_ptr, &w, sizeof(WORD));
    m_ptr += sizeof(WORD);
  }
  void writeDWord(DWORD d)
  {
    memcpy(m_ptr, &d, sizeof(DWORD));
    m_ptr += sizeof(DWORD);
  }
  void writeString(const TCHAR *s)
  {
    size_t len = _tcslen(s);
    memcpy(m_ptr, s, (len + 1) * sizeof(TCHAR));
    m_ptr += (len + 1) * sizeof(TCHAR);
  }

  BYTE *m_ptr;
};

} // namespace

ConnectionConfigDialog::ConnectionConfigDialog()
: BaseDialog(), m_config(0), m_parentDialog(NULL), m_serverConfigDialog(0),
  m_eyeIcon(0), m_mouseIcon(0)
{
  // This page has no compiled .rc dialog resource. Build a minimal
  // DLGTEMPLATE (0 controls; everything is added at runtime in
  // onInitDialog(), same as the extra controls the other pages add) with
  // the same WS_CHILD/font style as the other tab pages (see
  // IDD_CONFIG_SERVER_PAGE in tvnserver.rc: "STYLE DS_SETFONT | WS_CHILD",
  // "EXSTYLE WS_EX_CONTROLPARENT", "FONT 8, MS Shell Dlg").
  TemplateWriter w(m_templateBuffer);
  w.writeDWord(DS_SETFONT | WS_CHILD);
  w.writeDWord(WS_EX_CONTROLPARENT);
  w.writeWord(0);   // cdit: no controls in the template itself
  w.writeWord(0);   // x
  w.writeWord(0);   // y
  w.writeWord(306); // cx
  w.writeWord(205); // cy
  w.writeWord(0);   // no menu
  w.writeWord(0);   // default dialog window class
  w.writeString(_T(""));
  w.writeWord(8);   // font point size
  w.writeString(_T("MS Shell Dlg"));

  setDialogTemplate((LPCDLGTEMPLATE)m_templateBuffer);
}

ConnectionConfigDialog::~ConnectionConfigDialog()
{
}

void ConnectionConfigDialog::setParentDialog(BaseDialog *dialog)
{
  m_parentDialog = dialog;
}

void ConnectionConfigDialog::setServerConfigDialog(ServerConfigDialog *dialog)
{
  m_serverConfigDialog = dialog;
}

BOOL ConnectionConfigDialog::onInitDialog()
{
  m_config = Configurator::getInstance()->getServerConfig();

  HWND parent = m_ctrlThis.getWindow();
  HFONT dlgFont = (HFONT)SendMessage(parent, WM_GETFONT, 0, 0);
  HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE);

  const int iconSize = 16;
  const int rowHeight = 26;
  const int iconLeft = 16;
  const int checkboxLeft = iconLeft + iconSize + 8;
  RECT parentRc;
  GetClientRect(parent, &parentRc);
  int checkboxWidth = parentRc.right - checkboxLeft - 16;
  if (checkboxWidth < 260) {
    checkboxWidth = 260;
  }
  int rowTop = 16;

  // Start GDI+ once for both icon loads below (loadIconBitmap() no longer
  // starts/stops it itself).
  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken = 0;
  bool gdiplusStarted =
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, 0) == Ok;

  m_eyeIcon = gdiplusStarted ? loadIconBitmap(_T("eye.png"), iconSize) : 0;
  if (m_eyeIcon != 0) {
    HWND iconHwnd = CreateWindowEx(0, _T("STATIC"), _T(""),
      WS_CHILD | WS_VISIBLE | SS_BITMAP,
      iconLeft, rowTop + 1, iconSize, iconSize,
      parent, (HMENU)0, hInstance, 0);
    if (iconHwnd != 0) {
      SendMessage(iconHwnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)m_eyeIcon);
    }
  }
  HWND screenGuardHwnd = CreateWindowEx(0, _T("BUTTON"),
    _T("Show screen guard"),
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
    checkboxLeft, rowTop, checkboxWidth, 18,
    parent, (HMENU)IDC_SCREEN_GUARD, hInstance, 0);
  if (screenGuardHwnd != 0 && dlgFont != 0) {
    SendMessage(screenGuardHwnd, WM_SETFONT, (WPARAM)dlgFont, TRUE);
  }

  rowTop += rowHeight;

  m_mouseIcon = gdiplusStarted ? loadIconBitmap(_T("mouse.png"), iconSize) : 0;
  if (m_mouseIcon != 0) {
    HWND iconHwnd = CreateWindowEx(0, _T("STATIC"), _T(""),
      WS_CHILD | WS_VISIBLE | SS_BITMAP,
      iconLeft, rowTop + 1, iconSize, iconSize,
      parent, (HMENU)0, hInstance, 0);
    if (iconHwnd != 0) {
      SendMessage(iconHwnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)m_mouseIcon);
    }
  }
  HWND blockLocalInputHwnd = CreateWindowEx(0, _T("BUTTON"),
    _T("No local input during client sessions"),
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
    checkboxLeft, rowTop, checkboxWidth, 18,
    parent, (HMENU)IDC_CONNECTION_BLOCK_LOCAL_INPUT, hInstance, 0);
  if (blockLocalInputHwnd != 0 && dlgFont != 0) {
    SendMessage(blockLocalInputHwnd, WM_SETFONT, (WPARAM)dlgFont, TRUE);
  }

  if (gdiplusStarted) {
    GdiplusShutdown(gdiplusToken);
  }

  m_screenGuard.setWindow(GetDlgItem(parent, IDC_SCREEN_GUARD));
  m_blockLocalInput.setWindow(GetDlgItem(parent, IDC_CONNECTION_BLOCK_LOCAL_INPUT));

  updateUI();

  return TRUE;
}

BOOL ConnectionConfigDialog::onCommand(UINT controlID, UINT notificationID)
{
  if (notificationID == BN_CLICKED) {
    switch (controlID) {
    case IDC_SCREEN_GUARD:
      onScreenGuardClick();
      break;
    case IDC_CONNECTION_BLOCK_LOCAL_INPUT:
      onBlockLocalInputClick();
      break;
    }
  }
  return TRUE;
}

BOOL ConnectionConfigDialog::onDestroy()
{
  if (m_eyeIcon != 0) {
    DeleteObject(m_eyeIcon);
    m_eyeIcon = 0;
  }
  if (m_mouseIcon != 0) {
    DeleteObject(m_mouseIcon);
    m_mouseIcon = 0;
  }
  return TRUE;
}

void ConnectionConfigDialog::onScreenGuardClick()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ConnectionConfigDialog::onBlockLocalInputClick()
{
  if (m_serverConfigDialog != 0) {
    m_serverConfigDialog->setBlockLocalInputChecked(m_blockLocalInput.isChecked());
  }
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ConnectionConfigDialog::updateUI()
{
  m_screenGuard.check(m_config->isScreenGuardEnabled());

  bool blockLocalInput = m_config->isBlockingLocalInput();
  m_blockLocalInput.check(blockLocalInput);
  if (m_serverConfigDialog != 0) {
    m_serverConfigDialog->setBlockLocalInputChecked(blockLocalInput);
  }
}

void ConnectionConfigDialog::apply()
{
  m_config->enableScreenGuard(m_screenGuard.isChecked());
  // This is the interactive check box the user actually clicks; it is the
  // single source of truth for the setting. The hidden mirror on the
  // Server page (kept in sync via setBlockLocalInputChecked()) exists only
  // to drive ServerConfigDialog::updateCheckboxesState() and is never
  // itself persisted.
  m_config->blockLocalInput(m_blockLocalInput.isChecked());
}

HBITMAP ConnectionConfigDialog::loadIconBitmap(const TCHAR *fileName, int targetSize)
{
  // GDI+ must already be started by the caller (see onInitDialog()).
  StringStorage moduleFolder;
  if (!Environment::getCurrentModuleFolderPath(&moduleFolder)) {
    return 0;
  }
  StringStorage imagePath;
  imagePath.format(_T("%s\\%s"), moduleFolder.getString(), fileName);

  return LoadPngAsBitmap(imagePath.getString(), targetSize, targetSize);
}
