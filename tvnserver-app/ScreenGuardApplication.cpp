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

#include "ScreenGuardApplication.h"

#include "tvnserver/resource.h"

#include "win-system/WinCommandLineArgs.h"
#include "util/CommandLine.h"
#include "util/Exception.h"
#include "win-system/SharedMemory.h"
#include "win-system/Environment.h"

#include <gdiplus.h>

using namespace Gdiplus;

// Link the libraries needed by GDI+ (logo drawing) and AlphaBlend
// directly from the code, so no .vcxproj edits are required.
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

const TCHAR ScreenGuardSharedNames::SHARED_MEMORY_NAME[] =
  _T("Global\\TightVNC Screen Guard State");

const TCHAR ScreenGuardApplication::SCREEN_GUARD_KEY[] = _T("-screenguard");

// Test key: forces the protection windows to be shown regardless of the
// published state. Useful for diagnosing the overlay/banner windows.
static const TCHAR SCREEN_GUARD_TEST_KEY[] = _T("-screenguardtest");

static const TCHAR OVERLAY_WINDOW_CLASS[] = _T("TightVNC Screen Guard Overlay");
static const TCHAR BANNER_WINDOW_CLASS[]   = _T("TightVNC Screen Guard Banner");
static const TCHAR BLANK_WINDOW_CLASS[]    = _T("TightVNC Screen Guard Blank");

// Window styles used for all guard windows.
static const DWORD GUARD_WINDOW_STYLE =
  WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN;

static const DWORD GUARD_WINDOW_EX_STYLE =
  WS_EX_TOPMOST |       // Always above other windows.
  WS_EX_TOOLWINDOW |    // Do not show on the task bar and in Alt+Tab.
  WS_EX_LAYERED |       // Needed for alpha blending.
  WS_EX_TRANSPARENT |   // Click-through: input goes to windows below.
  WS_EX_NOACTIVATE;     // Never activated, never steals focus.

// Overlay darkening level (alpha). The desktop under the overlay remains
// visible but the banner is clearly noticeable.
static const BYTE OVERLAY_ALPHA = 255;

// Guard timer interval.
static const UINT GUARD_TIMER_ID = 1;
static const UINT GUARD_TIMER_INTERVAL_MS = 500;

// The server refreshes the heartbeat each second. If there was no heartbeat
// for a longer period, the server is considered dead.
static const UINT SERVER_HEARTBEAT_TIMEOUT_MS = 5000;

// When the client count becomes zero, the guard stays alive for a while
// (in case of a quick reconnect) and then exits.
static const UINT ZERO_CLIENTS_EXIT_TICKS = 4; // 4 * 500 ms = 2 seconds

ScreenGuardApplication::ScreenGuardApplication(HINSTANCE hInstance,
                                               const TCHAR *windowClassName,
                                               const TCHAR *cmdLine)
: LocalWindowsApplication(hInstance, windowClassName),
  m_overlayWindow(0),
  m_bannerWindow(0),
  m_blankWindow(0),
  m_bannerIcon(0),
  m_logoBitmap(0),
  m_logoWidth(0),
  m_logoHeight(0),
  m_sharedMemory(0),
  m_sharedData(0),
  m_lastGeneration(0),
  m_lastClientCount(0),
  m_lastBlankEnabled(0),
  m_zeroClientTicks(0),
  m_testMode(false),
  m_cursorHidden(false),
  m_cmdLine(cmdLine)
{
}

ScreenGuardApplication::~ScreenGuardApplication()
{
  restoreLocalCursor();

  if (m_logoBitmap != 0) {
    DeleteObject(m_logoBitmap);
    m_logoBitmap = 0;
  }

  if (m_sharedMemory != 0) {
    delete (SharedMemory *)m_sharedMemory;
    m_sharedMemory = 0;
  }
}

void ScreenGuardApplication::initSharedObjects()
{
  m_sharedMemory = new SharedMemory(ScreenGuardSharedNames::SHARED_MEMORY_NAME,
                                    sizeof(ScreenGuardSharedData));
  m_sharedData =
    (ScreenGuardSharedData *)((SharedMemory *)m_sharedMemory)->getMemPointer();
}

int ScreenGuardApplication::run()
{
  // Initialize GDI+ (needed to load logo.png).
  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken = 0;
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, 0);

  // Parse the command line (the key must be present; we just validate it).
  CommandLineFormat format[] = {
    { SCREEN_GUARD_KEY, NO_ARG },
    { SCREEN_GUARD_TEST_KEY, NO_ARG }
  };
  CommandLine parser;
  try {
    WinCommandLineArgs cmdArgs(m_cmdLine.getString());
    if (!parser.parse(format, sizeof(format) / sizeof(CommandLineFormat),
                      &cmdArgs) ||
        (!parser.optionSpecified(SCREEN_GUARD_KEY) &&
         !parser.optionSpecified(SCREEN_GUARD_TEST_KEY))) {
      throw Exception(_T("The screen guard key is not specified"));
    }
    m_testMode = parser.optionSpecified(SCREEN_GUARD_TEST_KEY);
  } catch (Exception &) {
    // Wrong command line. Do not block the user session with an error
    // dialog, just exit.
    return 1;
  }

  try {
    initSharedObjects();
  } catch (Exception &) {
    // The shared memory cannot be opened. Most likely the server has
    // already stopped. Nothing to guard.
    MessageBox(0, _T("Screen guard: cannot open shared memory.\n")
                  _T("Is the TightVNC service running?"),
               _T("TightVNC Screen Guard"), MB_OK | MB_ICONERROR);
    return 1;
  }

  // Load the banner icon from the module resources.
  m_bannerIcon = LoadIcon(m_appInstance, MAKEINTRESOURCE(IDI_CONNECTED));

  // Load the logo image from the executable folder.
  loadLogo();

  try {
    registerWindowClasses();
    createOverlayWindow();
    createBannerWindow();
    createBlankWindow();
  } catch (Exception &) {
    destroyWindows();
    MessageBox(0, _T("Screen guard: cannot create protection windows."),
               _T("TightVNC Screen Guard"), MB_OK | MB_ICONERROR);
    return 1;
  }

  // Initial state.
  m_lastGeneration =
    InterlockedExchangeAdd(&m_sharedData->generation, 0);
  applySharedState();

  // Start the watchdog timer on the overlay window (it always exists).
  SetTimer(m_overlayWindow, GUARD_TIMER_ID, GUARD_TIMER_INTERVAL_MS, 0);

  int retCode = WindowsApplication::run();

  KillTimer(m_overlayWindow, GUARD_TIMER_ID);
  destroyWindows();

  // Shut down GDI+.
  GdiplusShutdown(gdiplusToken);

  return retCode;
}

void ScreenGuardApplication::registerWindowClasses()
{
  WNDCLASS wndClass;

  memset(&wndClass, 0, sizeof(WNDCLASS));
  wndClass.lpfnWndProc = overlayWndProc;
  wndClass.hInstance = m_appInstance;
  wndClass.lpszClassName = OVERLAY_WINDOW_CLASS;
  wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  if (RegisterClass(&wndClass) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw SystemException();
  }

  memset(&wndClass, 0, sizeof(WNDCLASS));
  wndClass.lpfnWndProc = bannerWndProc;
  wndClass.hInstance = m_appInstance;
  wndClass.lpszClassName = BANNER_WINDOW_CLASS;
  if (RegisterClass(&wndClass) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw SystemException();
  }

  memset(&wndClass, 0, sizeof(WNDCLASS));
  wndClass.lpfnWndProc = blankWndProc;
  wndClass.hInstance = m_appInstance;
  wndClass.lpszClassName = BLANK_WINDOW_CLASS;
  wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  if (RegisterClass(&wndClass) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw SystemException();
  }
}

void ScreenGuardApplication::getVirtualDesktopRect(RECT *rc)
{
  rc->left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  rc->top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  rc->right = rc->left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  rc->bottom = rc->top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

void ScreenGuardApplication::excludeWindowFromCapture(HWND hwnd)
{
  // Exclude the guard windows from screen capture so that remote clients
  // never see the guard overlay. On Windows 7 and earlier the function is
  // not available, so ignore the error.
  HMODULE user32 = GetModuleHandle(_T("user32.dll"));
  if (user32 == 0) {
    return;
  }
  typedef BOOL (WINAPI *SetWindowDisplayAffinityFn)(HWND, DWORD);
  SetWindowDisplayAffinityFn setDisplayAffinity =
    (SetWindowDisplayAffinityFn)GetProcAddress(user32,
                                               "SetWindowDisplayAffinity");
  if (setDisplayAffinity == 0) {
    return;
  }
  const DWORD WDA_EXCLUDEFROMCAPTURE = 0x00000011;
  setDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
}

void ScreenGuardApplication::createOverlayWindow()
{
  RECT rc;
  getVirtualDesktopRect(&rc);

  m_overlayWindow = CreateWindowEx(GUARD_WINDOW_EX_STYLE,
                                   OVERLAY_WINDOW_CLASS,
                                   _T(""),
                                   GUARD_WINDOW_STYLE,
                                   rc.left, rc.top,
                                   rc.right - rc.left, rc.bottom - rc.top,
                                   0, 0, m_appInstance, this);
  if (m_overlayWindow == 0) {
    throw SystemException();
  }

  // Make the overlay semi-transparent.
  SetLayeredWindowAttributes(m_overlayWindow, 0, OVERLAY_ALPHA, LWA_ALPHA);

  excludeWindowFromCapture(m_overlayWindow);
}

void ScreenGuardApplication::createBannerWindow()
{
  RECT rc;
  getVirtualDesktopRect(&rc);

  const int bannerWidth = 480;
  const int bannerHeight = 140;
  int bannerX = rc.left + ((rc.right - rc.left) - bannerWidth) / 2;
  int bannerY = rc.top + ((rc.bottom - rc.top) - bannerHeight) / 2;

  m_bannerWindow = CreateWindowEx(GUARD_WINDOW_EX_STYLE,
                                  BANNER_WINDOW_CLASS,
                                  _T(""),
                                  GUARD_WINDOW_STYLE,
                                  bannerX, bannerY,
                                  bannerWidth, bannerHeight,
                                  0, 0, m_appInstance, this);
  if (m_bannerWindow == 0) {
    throw SystemException();
  }

  // Required for a WS_EX_LAYERED window to actually be composited: without
  // at least one call to SetLayeredWindowAttributes, GDI painting on a
  // layered window never becomes visible.
  SetLayeredWindowAttributes(m_bannerWindow, 0, 255, LWA_ALPHA);

  excludeWindowFromCapture(m_bannerWindow);
}

void ScreenGuardApplication::createBlankWindow()
{
  RECT rc;
  getVirtualDesktopRect(&rc);

  m_blankWindow = CreateWindowEx(GUARD_WINDOW_EX_STYLE,
                                 BLANK_WINDOW_CLASS,
                                 _T(""),
                                 GUARD_WINDOW_STYLE,
                                 rc.left, rc.top,
                                 rc.right - rc.left, rc.bottom - rc.top,
                                 0, 0, m_appInstance, this);
  if (m_blankWindow == 0) {
    throw SystemException();
  }

  // Fully opaque black screen.
  SetLayeredWindowAttributes(m_blankWindow, 0, 255, LWA_ALPHA);

  excludeWindowFromCapture(m_blankWindow);

  // Hidden by default. It becomes visible when the server enables the
  // blank screen protection.
  ShowWindow(m_blankWindow, SW_HIDE);
}

void ScreenGuardApplication::destroyWindows()
{
  if (m_overlayWindow != 0) {
    DestroyWindow(m_overlayWindow);
    m_overlayWindow = 0;
  }
  if (m_bannerWindow != 0) {
    DestroyWindow(m_bannerWindow);
    m_bannerWindow = 0;
  }
  if (m_blankWindow != 0) {
    DestroyWindow(m_blankWindow);
    m_blankWindow = 0;
  }

  restoreLocalCursor();
}

void ScreenGuardApplication::loadLogo()
{
  // Look for logo.png in the same folder as tvnserver.exe.
  StringStorage moduleFolder;
  if (!Environment::getCurrentModuleFolderPath(&moduleFolder)) {
    return;
  }
  StringStorage logoPath;
  logoPath.format(_T("%s\\logo.png"), moduleFolder.getString());

  Bitmap *bitmap = 0;
  try {
    bitmap = Bitmap::FromFile(logoPath.getString());
  } catch (...) {
    bitmap = 0;
  }
  if (bitmap == 0 || bitmap->GetLastStatus() != Ok) {
    if (bitmap != 0) {
      delete bitmap;
    }
    return;
  }

  m_logoWidth = bitmap->GetWidth();
  m_logoHeight = bitmap->GetHeight();

  // Convert the GDI+ bitmap to a GDI HBITMAP for drawing with DrawImage/
  // BitBlt in the banner WM_PAINT handler.
  BITMAPINFO bmi;
  memset(&bmi, 0, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = m_logoWidth;
  bmi.bmiHeader.biHeight = -m_logoHeight; // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = 0;
  HDC screenDc = GetDC(0);
  HBITMAP hbmp = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS,
                                  &bits, 0, 0);
  HDC memDc = CreateCompatibleDC(screenDc);
  ReleaseDC(0, screenDc);
  if (hbmp == 0 || memDc == 0) {
    if (hbmp != 0) DeleteObject(hbmp);
    if (memDc != 0) DeleteDC(memDc);
    delete bitmap;
    return;
  }

  HGDIOBJ oldBmp = SelectObject(memDc, hbmp);
  Graphics graphics(memDc);
  graphics.DrawImage(bitmap, 0, 0, m_logoWidth, m_logoHeight);
  SelectObject(memDc, oldBmp);
  DeleteDC(memDc);
  delete bitmap;

  m_logoBitmap = hbmp;
}

// Standard OCR_* system cursor identifiers (winuser.h only declares them
// when OEMRESOURCE is defined before including windows.h). Hardcoded here
// to avoid touching the shared precompiled header.
static const UINT SYSTEM_CURSOR_IDS[] = {
  32512, // OCR_NORMAL
  32513, // OCR_IBEAM
  32514, // OCR_WAIT
  32515, // OCR_CROSS
  32516, // OCR_UP
  32642, // OCR_SIZENWSE
  32643, // OCR_SIZENESW
  32644, // OCR_SIZEWE
  32645, // OCR_SIZENS
  32646, // OCR_SIZEALL
  32648, // OCR_NO
  32649, // OCR_HAND
  32650, // OCR_APPSTARTING
};

void ScreenGuardApplication::hideLocalCursor()
{
  if (m_cursorHidden) {
    return;
  }

  // A fully transparent 32x32 cursor (AND mask all 1 = transparent, XOR
  // mask all 0). Used to replace every system cursor shape so the pointer
  // is hidden system-wide, including while the remote client moves it over
  // windows that do not belong to this process (ShowCursor() cannot do
  // that on Windows 8+, where its effect is scoped to the calling thread).
  const int cursorSize = 32;
  BYTE andMask[32 * 4];
  BYTE xorMask[32 * 4];
  memset(andMask, 0xFF, sizeof(andMask));
  memset(xorMask, 0, sizeof(xorMask));

  HCURSOR blankCursor = CreateCursor(m_appInstance, 0, 0, cursorSize,
                                     cursorSize, andMask, xorMask);
  if (blankCursor == 0) {
    return;
  }

  for (size_t i = 0;
       i < sizeof(SYSTEM_CURSOR_IDS) / sizeof(SYSTEM_CURSOR_IDS[0]); i++) {
    HCURSOR copy = CopyCursor(blankCursor);
    if (copy != 0) {
      // SetSystemCursor takes ownership of the handle and destroys it.
      SetSystemCursor(copy, SYSTEM_CURSOR_IDS[i]);
    }
  }
  DestroyCursor(blankCursor);

  m_cursorHidden = true;
}

void ScreenGuardApplication::restoreLocalCursor()
{
  if (!m_cursorHidden) {
    return;
  }
  // Resets every system cursor back to the user's configured scheme,
  // undoing all the SetSystemCursor calls above in a single step.
  SystemParametersInfo(SPI_SETCURSORS, 0, 0, 0);
  m_cursorHidden = false;
}

void ScreenGuardApplication::readSharedState(LONG *generation,
                                             LONG *clientCount,
                                             LONG *blankEnabled,
                                             StringStorage *clientAddress)
{
  // Re-read until the generation counter does not change during the read.
  LONG gen1;
  LONG gen2;
  do {
    gen1 = InterlockedExchangeAdd(&m_sharedData->generation, 0);
    *clientCount = InterlockedExchangeAdd(&m_sharedData->clientCount, 0);
    *blankEnabled =
      InterlockedExchangeAdd(&m_sharedData->blankScreenEnabled, 0);
    clientAddress->setString(m_sharedData->lastClientAddress);
    gen2 = InterlockedExchangeAdd(&m_sharedData->generation, 0);
  } while (gen1 != gen2);
  *generation = gen1;
}

void ScreenGuardApplication::applySharedState()
{
  LONG generation;
  LONG clientCount;
  LONG blankEnabled;
  StringStorage clientAddress;

  readSharedState(&generation, &clientCount, &blankEnabled, &clientAddress);

  bool stateChanged = (generation != m_lastGeneration);

  if (stateChanged) {
    RECT rc;
    getVirtualDesktopRect(&rc);

    // Overlay covers the whole desktop.
    SetWindowPos(m_overlayWindow, HWND_TOPMOST,
                 rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE);

    // Banner is centered on the desktop.
    RECT bannerRc;
    GetWindowRect(m_bannerWindow, &bannerRc);
    int bannerWidth = bannerRc.right - bannerRc.left;
    int bannerHeight = bannerRc.bottom - bannerRc.top;
    int bannerX = rc.left + ((rc.right - rc.left) - bannerWidth) / 2;
    int bannerY = rc.top + ((rc.bottom - rc.top) - bannerHeight) / 2;
    SetWindowPos(m_bannerWindow, HWND_TOPMOST,
                 bannerX, bannerY, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);

    // Black screen window covers the whole desktop. It must be above the
    // overlay and the banner when visible, so it is always reordered last.
    SetWindowPos(m_blankWindow, HWND_TOPMOST,
                 rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE);

    m_lastGeneration = generation;
    m_lastClientCount = clientCount;
    m_lastBlankEnabled = blankEnabled;
  }

  // Overlay and banner are visible while at least one client is connected
  // (or always in the test mode).
  bool showGuard = m_testMode || clientCount > 0;
  ShowWindow(m_overlayWindow,
             showGuard ? SW_SHOWNOACTIVATE : SW_HIDE);
  ShowWindow(m_bannerWindow,
             showGuard ? SW_SHOWNOACTIVATE : SW_HIDE);

  // Hide the local mouse cursor while the guard is visible so the local
  // user cannot see the remote-controlled cursor movement. Restore it when
  // the guard is hidden.
  if (showGuard) {
    hideLocalCursor();
  } else {
    restoreLocalCursor();
  }

  // The black screen is visible only when it is enabled.
  ShowWindow(m_blankWindow,
             showGuard && (m_testMode || blankEnabled) ?
               SW_SHOWNOACTIVATE : SW_HIDE);

  // Force the banner repaint when the state changed.
  if (stateChanged) {
    InvalidateRect(m_bannerWindow, 0, TRUE);
  }
}

bool ScreenGuardApplication::isServerAlive()
{
  ULONG heartbeat =
    InterlockedExchangeAdd(&m_sharedData->serverHeartbeatMs, 0);
  ULONG now = GetTickCount();
  ULONG age = now - (ULONG)heartbeat;
  return age < SERVER_HEARTBEAT_TIMEOUT_MS;
}

void ScreenGuardApplication::onTimerTick()
{
  applySharedState();

  LONG clientCount =
    InterlockedExchangeAdd(&m_sharedData->clientCount, 0);

  // In the test mode the guard never exits by itself.
  if (m_testMode) {
    return;
  }

  if (!isServerAlive()) {
    WindowsApplication::shutdown();
    return;
  }

  if (clientCount == 0) {
    m_zeroClientTicks++;
    if (m_zeroClientTicks >= ZERO_CLIENTS_EXIT_TICKS) {
      WindowsApplication::shutdown();
      return;
    }
  } else {
    m_zeroClientTicks = 0;
  }
}

void ScreenGuardApplication::drawBanner(HDC hdc, const RECT *rc)
{
  RECT clientRc = *rc;
  int width = clientRc.right - clientRc.left;
  int height = clientRc.bottom - clientRc.top;

  // Background: white interior with a dark blue border.
  HBRUSH fillBrush = CreateSolidBrush(RGB(255, 255, 255));
  HPEN borderPen = CreatePen(PS_SOLID, 3, RGB(0, 70, 160));

  HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, fillBrush);
  HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);

  Rectangle(hdc, clientRc.left, clientRc.top,
            clientRc.right, clientRc.bottom);

  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBrush);
  DeleteObject(borderPen);
  DeleteObject(fillBrush);

  // Logo image (logo.png next to the executable). Falls back to the
  // built-in icon when the file is not available.
  int textLeft = 64;
  if (m_logoBitmap != 0 && m_logoWidth > 0 && m_logoHeight > 0) {
    HDC memDc = CreateCompatibleDC(hdc);
    HGDIOBJ oldBmp = SelectObject(memDc, m_logoBitmap);

    // Fit the logo into a 64x64 box keeping its aspect ratio.
    int drawW = m_logoWidth;
    int drawH = m_logoHeight;
    const int maxLogoSize = 64;
    if (drawW > maxLogoSize || drawH > maxLogoSize) {
      double scale = min((double)maxLogoSize / drawW,
                         (double)maxLogoSize / drawH);
      drawW = (int)(drawW * scale);
      drawH = (int)(drawH * scale);
    }
    int logoX = 8 + (64 - drawW) / 2;
    int logoY = (height - drawH) / 2;

    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, logoX, logoY, drawW, drawH,
               memDc, 0, 0, m_logoWidth, m_logoHeight, blend);

    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);

    textLeft = 80;
  } else if (m_bannerIcon != 0) {
    int iconX = 16;
    int iconY = (height - 32) / 2;
    DrawIconEx(hdc, iconX, iconY, m_bannerIcon, 32, 32, 0, 0, DI_NORMAL);
  }

  // Warning message.
  RECT textRc;
  textRc.left = textLeft;
  textRc.top = 20;
  textRc.right = width - 16;
  textRc.bottom = height - 20;

  HFONT titleFont = CreateFont(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE,
                               _T("MS Shell Dlg"));

  SetBkMode(hdc, TRANSPARENT);

  HFONT oldFont = (HFONT)SelectObject(hdc, titleFont);
  SetTextColor(hdc, RGB(0, 70, 160));
  DrawText(hdc, _T("Computador em manutenção, não desligar!"),
           -1, &textRc, DT_LEFT | DT_TOP | DT_WORDBREAK);

  SelectObject(hdc, oldFont);
  DeleteObject(titleFont);
}

LRESULT CALLBACK ScreenGuardApplication::overlayWndProc(HWND hWnd, UINT msg,
                                                        WPARAM wparam,
                                                        LPARAM lparam)
{
  ScreenGuardApplication *_this = 0;

  if (msg == WM_NCCREATE) {
    CREATESTRUCT *cs = (CREATESTRUCT *)lparam;
    _this = (ScreenGuardApplication *)cs->lpCreateParams;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)_this);
    _this->m_overlayWindow = hWnd;
  } else {
    _this = (ScreenGuardApplication *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
  }

  switch (msg) {
  case WM_TIMER:
    if (_this != 0 && wparam == GUARD_TIMER_ID) {
      _this->onTimerTick();
      return 0;
    }
    break;
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT rc;
    GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    EndPaint(hWnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  }
  return DefWindowProc(hWnd, msg, wparam, lparam);
}

LRESULT CALLBACK ScreenGuardApplication::bannerWndProc(HWND hWnd, UINT msg,
                                                       WPARAM wparam,
                                                       LPARAM lparam)
{
  ScreenGuardApplication *_this = 0;

  if (msg == WM_NCCREATE) {
    CREATESTRUCT *cs = (CREATESTRUCT *)lparam;
    _this = (ScreenGuardApplication *)cs->lpCreateParams;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)_this);
    _this->m_bannerWindow = hWnd;
  } else {
    _this = (ScreenGuardApplication *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
  }

  switch (msg) {
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT rc;
    GetClientRect(hWnd, &rc);
    if (_this != 0) {
      _this->drawBanner(hdc, &rc);
    }
    EndPaint(hWnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  }
  return DefWindowProc(hWnd, msg, wparam, lparam);
}

LRESULT CALLBACK ScreenGuardApplication::blankWndProc(HWND hWnd, UINT msg,
                                                      WPARAM wparam,
                                                      LPARAM lparam)
{
  switch (msg) {
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT rc;
    GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    EndPaint(hWnd, &ps);
    return 0;
  }
  case WM_SETCURSOR:
    SetCursor(0);
    return TRUE;
  case WM_ERASEBKGND:
    return 1;
  }
  return DefWindowProc(hWnd, msg, wparam, lparam);
}
