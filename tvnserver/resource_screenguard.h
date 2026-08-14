// Copyright (C) 2025
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

// Resource identifiers for the screen guard feature.
//
// The screen guard check box is created at runtime by ServerConfigDialog,
// so this header defines only the identifiers, not the dialog resources.
// This avoids editing the UTF-16 resource files (tvnserver.rc and
// resource.h) which is error-prone outside Visual Studio.

#ifndef __RESOURCE_SCREEN_GUARD_H__
#define __RESOURCE_SCREEN_GUARD_H__

// Check box "Show screen guard to the local user" on the Server page.
// The value 1098 is free: the highest control id used in resource.h is
// 1097 (IDC_CONNECT_RDP_SESSION).
#define IDC_SCREEN_GUARD 1098

#endif // __RESOURCE_SCREEN_GUARD_H__
