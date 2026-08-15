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

#ifndef _CONNECTION_CONFIG_DIALOG_H_
#define _CONNECTION_CONFIG_DIALOG_H_

#include "gui/BaseDialog.h"
#include "gui/CheckBox.h"
#include "server-config-lib/ServerConfig.h"

class ServerConfigDialog;

// "Connection" tab: the first tab of the server configuration dialog. It
// has no compiled .rc dialog resource of its own -- it is built from a
// minimal in-memory DLGTEMPLATE (see the constructor), so adding it does
// not require editing the UTF-16 tvnserver.rc file. All of its controls
// are created at runtime in onInitDialog(), same as the extra controls
// added by ServerConfigDialog.
class ConnectionConfigDialog : public BaseDialog
{
public:
  ConnectionConfigDialog();
  virtual ~ConnectionConfigDialog();

  void setParentDialog(BaseDialog *dialog);

  // "No local input during client sessions" still lives (hidden) on the
  // Server page too, because its Input Handling group depends on that
  // state (see ServerConfigDialog::updateCheckboxesState()). This keeps
  // the two in sync.
  void setServerConfigDialog(ServerConfigDialog *dialog);

  virtual BOOL onInitDialog();
  virtual BOOL onCommand(UINT controlID, UINT notificationID);
  virtual BOOL onDestroy();

  void updateUI();
  void apply();

private:
  void onScreenGuardClick();
  void onBlockLocalInputClick();

  // Loads a small PNG icon (from the executable's folder) and returns a
  // ready-to-use HBITMAP, or 0 when the file is not found.
  HBITMAP loadIconBitmap(const TCHAR *fileName, int targetSize);

  ServerConfig *m_config;
  BaseDialog *m_parentDialog;
  ServerConfigDialog *m_serverConfigDialog;

  CheckBox m_screenGuard;
  CheckBox m_blockLocalInput;

  HBITMAP m_eyeIcon;
  HBITMAP m_mouseIcon;

  // In-memory dialog template backing this page (see setDialogTemplate()).
  __declspec(align(4)) BYTE m_templateBuffer[512];
};

#endif
