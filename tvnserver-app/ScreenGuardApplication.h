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

#ifndef _SCREEN_GUARD_APPLICATION_H_
#define _SCREEN_GUARD_APPLICATION_H_

#include "util/CommonHeader.h"

#include "win-system/LocalWindowsApplication.h"

/**
 * Application that shows a visible screen protection to the local user
 * while remote clients are connected (like the TeamViewer screen guard).
 *
 * Design:
 *  - TvnServer publishes the connection status into a named shared memory
 *    block (see ScreenGuardSharedNames / ScreenGuardSharedData below).
 *  - TvnServer starts this application in the interactive user session when
 *    the first client connects. When the last client disconnects (or the
 *    server dies), this application terminates itself.
 *  - This application shows:
 *      1) A topmost semi-transparent overlay window that darkens the whole
 *         virtual desktop.
 *      2) A topmost opaque banner window with a warning message and the
 *         address of the connected client.
 *      3) Optionally a full screen black window that completely hides the
 *         desktop from the local user (blank screen protection).
 *  - All windows are click-through (they never take focus and never steal
 *    keyboard or mouse input), so the remote session keeps working normally
 *    while the local user sees the warning or the black screen.
 *  - All windows are marked with SetWindowDisplayAffinity
 *    (WDA_EXCLUDEFROMCAPTURE) when the OS supports it, so they are not
 *    captured by screen grabbing. The remote client sees the normal desktop.
 */

// Shared memory block name. It is shared between the server (writer) and
// the screen guard processes that run in interactive user sessions.
class ScreenGuardSharedNames
{
public:
  static const TCHAR SHARED_MEMORY_NAME[];
};

// Layout of the shared memory block. The server is the writer and the
// screen guard applications are readers.
#pragma pack(push, 4)
struct ScreenGuardSharedData
{
  // Generation counter. Incremented by the server every time it publishes
  // a new state. The guard process re-reads the state when the generation
  // changes.
  volatile LONG generation;

  // Server heartbeat, milliseconds (GetTickCount based). Refreshed by the
  // server roughly every second. The guard process terminates itself when
  // the heartbeat becomes too old.
  volatile ULONG serverHeartbeatMs;

  // Number of connected (authenticated) clients.
  volatile LONG clientCount;

  // Text information about the last authenticated client (zero terminated).
  TCHAR lastClientAddress[64];

  // When non zero, the guard process must show a black screen that hides
  // the desktop from the local user.
  volatile LONG blankScreenEnabled;
};
#pragma pack(pop)

class ScreenGuardApplication : public LocalWindowsApplication
{
public:
  /**
   * Key identifying screen guard action in the tvnserver.exe command line.
   */
  static const TCHAR SCREEN_GUARD_KEY[];

  /**
   * Creates instance of application.
   * @throws SystemException on fail (inherited from superclass).
   */
  ScreenGuardApplication(HINSTANCE hInstance,
                         const TCHAR *windowClassName,
                         const TCHAR *cmdLine);
  /**
   * Deletes instance.
   */
  virtual ~ScreenGuardApplication();

  /**
   * Runs the application: connects to the shared memory block, shows the
   * protection windows and runs the message loop until the server shuts
   * the guard down or the server heartbeat dies.
   * @return 0 on success, non-zero on error.
   */
  virtual int run();

  // Called by the guard timer. Applies the shared state and terminates the
  // application when the server is gone or no clients are connected.
  void onTimerTick();

  // Draws the warning banner. Called from the banner window procedure.
  void drawBanner(HDC hdc, const RECT *rc);

  // Loads the logo image (logo.png next to tvnserver.exe) into
  // m_logoBitmap. Does nothing when the file is not found.
  void loadLogo();

  // Hides the local mouse cursor. Called when the guard becomes visible.
  void hideLocalCursor();

  // Restores the local mouse cursor. Called when the guard exits.
  void restoreLocalCursor();

protected:
  // Registers window classes used by the guard. @throws SystemException.
  void registerWindowClasses();

  // Creates the protection windows. @throws SystemException on fail.
  void createOverlayWindow();
  void createBannerWindow();
  void createBlankWindow();

  // Destroys all created windows.
  void destroyWindows();

  // Updates window positions and visibility from the shared memory data.
  void applySharedState();

  // Opens the shared memory. @throws Exception on fail.
  void initSharedObjects();

  // Tries to exclude the window from screen capture. Does nothing when the
  // current OS does not support it.
  void excludeWindowFromCapture(HWND hwnd);

  // Fills the rc argument with the current virtual desktop rectangle.
  void getVirtualDesktopRect(RECT *rc);

  // Reads the published state with the generation lock protocol.
  void readSharedState(LONG *generation, LONG *clientCount,
                       LONG *blankEnabled, StringStorage *clientAddress);

  // Returns true if the server is considered alive.
  bool isServerAlive();

  static LRESULT CALLBACK overlayWndProc(HWND hWnd, UINT msg,
                                         WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK bannerWndProc(HWND hWnd, UINT msg,
                                        WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK blankWndProc(HWND hWnd, UINT msg,
                                       WPARAM wparam, LPARAM lparam);

  // The windows.
  HWND m_overlayWindow;
  HWND m_bannerWindow;
  HWND m_blankWindow;

  // Banner icon.
  HICON m_bannerIcon;

  // Logo image loaded from logo.png (may be zero when not available).
  HBITMAP m_logoBitmap;
  int m_logoWidth;
  int m_logoHeight;

  // Shared memory object. Keeps the mapping alive during the whole
  // application run.
  class SharedMemory *m_sharedMemory;
  ScreenGuardSharedData *m_sharedData;

  // Last applied state (used to redraw only when the state changes).
  LONG m_lastGeneration;
  LONG m_lastClientCount;
  LONG m_lastBlankEnabled;

  // How many timer ticks the client count has been zero.
  UINT m_zeroClientTicks;

  // When true, the guard shows the protection windows regardless of the
  // published client count. Used for testing ("-screenguardtest").
  bool m_testMode;

  // True while the guard has replaced the system cursors with a blank one
  // (via SetSystemCursor) so the local user cannot see the remote-controlled
  // mouse movement. Restored with SystemParametersInfo(SPI_SETCURSORS).
  bool m_cursorHidden;

  // Command line.
  StringStorage m_cmdLine;
};

#endif // _SCREEN_GUARD_APPLICATION_H_
