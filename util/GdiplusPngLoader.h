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

#ifndef __GDIPLUS_PNG_LOADER_H__
#define __GDIPLUS_PNG_LOADER_H__

#include "util/CommonHeader.h"
#include <gdiplus.h>

// Loads and decodes an image file with GDI+. Returns 0 (and deletes any
// partially-constructed Bitmap) if the file does not exist or cannot be
// decoded. GDI+ must already be started (GdiplusStartup) by the caller.
// Caller owns the returned Bitmap and must delete it.
inline Gdiplus::Bitmap *LoadGdiplusBitmapFromFile(const TCHAR *filePath)
{
  using namespace Gdiplus;

  Bitmap *bitmap = 0;
  try {
    bitmap = Bitmap::FromFile(filePath);
  } catch (...) {
    bitmap = 0;
  }
  if (bitmap == 0 || bitmap->GetLastStatus() != Ok) {
    if (bitmap != 0) {
      delete bitmap;
    }
    return 0;
  }
  return bitmap;
}

// Loads a PNG file with GDI+ and converts it to a top-down 32bpp HBITMAP.
// Pass targetWidth/targetHeight <= 0 to keep the source image's native
// size (no scaling); otherwise the image is scaled to that size.
// outSourceWidth/outSourceHeight, if not null, receive the source image's
// native pixel size. Returns 0 if the file does not exist or cannot be
// decoded. GDI+ must already be started (GdiplusStartup) by the caller.
inline HBITMAP LoadPngAsBitmap(const TCHAR *filePath,
                               int targetWidth, int targetHeight,
                               int *outSourceWidth = 0,
                               int *outSourceHeight = 0)
{
  using namespace Gdiplus;

  Bitmap *bitmap = LoadGdiplusBitmapFromFile(filePath);
  if (bitmap == 0) {
    return 0;
  }

  int sourceWidth = (int)bitmap->GetWidth();
  int sourceHeight = (int)bitmap->GetHeight();
  if (outSourceWidth != 0) {
    *outSourceWidth = sourceWidth;
  }
  if (outSourceHeight != 0) {
    *outSourceHeight = sourceHeight;
  }

  int width = targetWidth > 0 ? targetWidth : sourceWidth;
  int height = targetHeight > 0 ? targetHeight : sourceHeight;

  BITMAPINFO bmi;
  memset(&bmi, 0, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height; // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = 0;
  HDC screenDc = GetDC(0);
  HBITMAP hbmp = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, 0, 0);
  HDC memDc = CreateCompatibleDC(screenDc);
  ReleaseDC(0, screenDc);

  HBITMAP result = 0;
  if (hbmp != 0 && memDc != 0) {
    HGDIOBJ oldBmp = SelectObject(memDc, hbmp);
    Graphics graphics(memDc);
    graphics.DrawImage(bitmap, 0, 0, width, height);
    SelectObject(memDc, oldBmp);
    result = hbmp;
  } else if (hbmp != 0) {
    DeleteObject(hbmp);
  }
  if (memDc != 0) {
    DeleteDC(memDc);
  }

  delete bitmap;
  return result;
}

#endif // __GDIPLUS_PNG_LOADER_H__
