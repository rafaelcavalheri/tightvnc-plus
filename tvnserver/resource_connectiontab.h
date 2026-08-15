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

// Resource identifier for the "Connection" tab page (ConnectionConfigDialog).
// The page itself has no compiled .rc dialog resource -- it is built from an
// in-memory DLGTEMPLATE at runtime (see ConnectionConfigDialog.cpp), and its
// controls are created at runtime too, the same way resource_screenguard.h
// avoids editing the UTF-16 resource files.

#ifndef __RESOURCE_CONNECTION_TAB_H__
#define __RESOURCE_CONNECTION_TAB_H__

// Check box "No local input during client sessions", mirrored here from
// the (now hidden) IDC_BLOCK_LOCAL_INPUT check box on the Server page.
// The value 1099 is free: the highest control id used in resource.h is
// 1097 (IDC_CONNECT_RDP_SESSION), and 1098 is IDC_SCREEN_GUARD.
#define IDC_CONNECTION_BLOCK_LOCAL_INPUT 1099

#endif // __RESOURCE_CONNECTION_TAB_H__
