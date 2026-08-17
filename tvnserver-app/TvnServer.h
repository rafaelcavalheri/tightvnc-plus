// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef _TVN_SERVER_H_
#define _TVN_SERVER_H_

#include "util/CommonHeader.h"

#include "desktop/WinServiceDesktopFactory.h"
#include "desktop/ApplicationDesktopFactory.h"
#include "RfbClientManager.h"
#include "RfbServer.h"
#include "ExtraRfbServers.h"
#include "ControlServer.h"
#include "TvnServerListener.h"

#include "http-server-lib/HttpServer.h"

#include "thread/ZombieKiller.h"
#include "thread/LocalMutex.h"
#include "log-writer/LogWriter.h"
#include "util/Singleton.h"
#include "util/ListenerContainer.h"
#include "NewConnectionEvents.h"

#include "server-config-lib/Configurator.h"

#include "tvncontrol-app/TvnServerInfo.h"
#include "LogInitListener.h"

#include "ScreenGuardApplication.h"
#include "win-system/Process.h"

/**
 * TightVNC server singleton that includes serveral components:
 *   1) Zombie killer singleton.
 *   2) Configurator singleton.
 *   3) Log singleton.
 *   4) Rfb servers (main rfb server and extra servers).
 *   5) Http server.
 *   6) Control server.
 *   7) Other features:
     1) Do action when last client disconnects.
 */
class TvnServer : public Singleton<TvnServer>,
                  public ListenerContainer<TvnServerListener *>,
                  public ConfigReloadListener,
                  public RfbClientManagerEventListener
{
public:
  /**
   * Creates and starts TightVNC server execution (in separate thread).
   *
   * Makes sereval steps:
   *  1) Instanizes zombie killer.
   *  2) Instanizes configurator and load configuration.
   *  3) Instanizes log.
   *  4) Starts all servers.
   *
   * @param runsInServiceContext must be set to true if TvnServer is running in service context,
   * false, if in context of single application. Parameter determinates control client behavour and
   * initial place for loading TightVNC configuration.
   *
   * @remark doesn't block calling thread execution cause all servers runs in it's own threads.
   * To know when need to shutdown TightVNC server you need to use addListener method.
   */
  TvnServer(bool runsInServiceContext,
            NewConnectionEvents *newConnectionEvents,
            LogInitListener *logInitListener,
            Logger *logger);
  /**
   * Stops and destroys TightVNC server.
   * @remark don't generate shutdown signal(like shutdown() method does) for listeners.
   */
  virtual ~TvnServer();

  /**
   * Fills structure with information of current state of TvnServer.
   * @param info [out] output parameter that will contain TightVNC server information
   * after call of this method.
   * @fixme place extended information to server info.
   */
  void getServerInfo(TvnServerInfo *info);

  /**
   * Inherited from ConfigReloadListener interface to catch configuration reload event.
   *
   * Make several things:
   *  1) Changes log level.
   *  2) Restarts rfb servers.
   *  3) Restarts http server.
   */
  virtual void onConfigReload(ServerConfig *serverConfig);

  /**
   * Only generates shutdown signal (event) for TvnServer listeners.
   *
   * @remark used by ControlClient, when it recieves command to shutdown TightVNC.
   * @remark doesn't stop TightVNC server.
   * @fixme rename this method.
   */
  void generateExternalShutdownSignal();

  /**
   * Checks if TightVNC server runs in service context.
   * @returns true if runs in service context.
   * @deprecated use getServerInfo() instead or move to private.
   */
  bool isRunningAsService() const;

  /**
   * Implemented from RfbClientManagerEventListener.
   *
   * Does nothing.
   */
  virtual void afterFirstClientConnect();

  /**
   * Implemented from RfbClientManagerEventListener.
   *
   * Does specifed in configuration action when last client disconnects from
   * rfb server.
   */
  virtual void afterLastClientDisconnect();

  /**
   * Implemented from RfbClientManagerEventListener.
   *
   * Publishes the current client state to the screen guard shared memory
   * when the number of connected clients changes.
   */
  virtual void afterClientCountChanged();

protected:
  void restartHttpServer();
  void restartControlServer();
  void restartMainRfbServer();

  void stopHttpServer();
  void stopControlServer();
  void stopMainRfbServer();

  // Calls a callback function to change update log properties.
  void changeLogProps();

  // ---------- Screen guard support ----------

  // Starts the screen guard application in the interactive user session.
  // Does nothing if it is already running.
  void startScreenGuard();

  // Starts only the screen guard process in the current interactive session.
  // The heartbeat timer is managed by startScreenGuard().
  bool launchScreenGuardProcess();

  // Restarts the screen guard process if the active interactive session
  // changed while remote clients are still connected.
  void ensureScreenGuardInCurrentSession();

  // Returns the session where the screen guard should be visible.
  DWORD getScreenGuardTargetSessionId();

  // Requests the screen guard application to exit and stops it after a
  // short grace period.
  void stopScreenGuard();

  // Publishes the current client state into the shared memory block used
  // by the screen guard. Called on client connect/disconnect events.
  void publishScreenGuardState();

  // Publishes the heartbeat into the shared memory block. Called from the
  // guard heartbeat timer.
  void refreshScreenGuardHeartbeat();

  // Returns true if the screen guard application has been started.
  bool isScreenGuardRunning();

  // Called by the timer queue thread to refresh the guard heartbeat.
  static void CALLBACK screenGuardHeartbeatTimer(void *lpParameter,
                                                 BOOLEAN timerOrWaitFired);

  // ---------- Control Interface tray icon session follower ----------

  // Starts the watcher timer that keeps the Control Interface tray icon
  // running in the active interactive session. Called once from the
  // constructor when running as a service; unlike the screen guard, this
  // does not depend on any client being connected, so the local user
  // always has the tray icon available right after logging on.
  void startTrayIconWatcher();

  // Stops the watcher timer. Does not kill an already-running tray icon
  // process: it is left to reconnect or exit on its own, the same as one
  // opened manually by the user.
  void stopTrayIconWatcher();

  // Makes sure the tray icon process is running in the current
  // interactive session, (re)launching it as needed. No-op in application
  // mode, where the application window already provides its own tray icon.
  void ensureTrayIconInCurrentSession();

  // Called by the timer queue thread to run ensureTrayIconInCurrentSession().
  static void CALLBACK trayIconWatcherTimer(void *lpParameter,
                                            BOOLEAN timerOrWaitFired);

  /**
   * Log writer.
   */
  LogWriter m_log;
  ZombieKiller m_zombieKiller;

  Configurator m_config;
  /**
   * Shortcut to global server configuration.
   */
  ServerConfig *m_srvConfig;

  /**
   * Mutex for protecting servers.
   */
  LocalMutex m_mutex;

  /**
   * Flag that determitates if we run in server context.
   * true if service, false if application.
   */
  const bool m_runAsService;

  WinServiceDesktopFactory m_serviceDesktopFactory;
  ApplicationDesktopFactory m_applicationDesktopFactory;
  /**
   * Rfb client manager (for all rfb servers), used by rfb servers
   * rfb clients, control server and control clients.
   */
  RfbClientManager *m_rfbClientManager;
  /**
   * Control server.
   */
  ControlServer *m_controlServer;
  /**
   * Builtin http server.
   */
  HttpServer *m_httpServer;
  /**
   * Main rfb server.
   */
  RfbServer *m_rfbServer;
  /**
   * Extra servers for extra ports. This object is not protected by any mutex
   * and it does not implement any internal locking, so it should be used with
   * caution. Here we change its state on owner creation, on owner deletion
   * and on each configuration change (via a listener function called from
   * other threads). The listener function is registered after the object
   * creation and unregistered before the owner destruction, and its calls are
   * properly synchronized. Thus, we can be sure that m_extraRfbServers is not
   * used by different threads simultaneously.
   */
  ExtraRfbServers m_extraRfbServers;

  LogInitListener *m_logInitListener;

  UINT m_contextSwitchResolution; // in ms

  // ---------- Screen guard members ----------

  // Process object of the running screen guard application. Zero when the
  // guard is not running.
  Process *m_screenGuardProcess;

  // True when the screen guard application has been started and the stop
  // request has not been sent yet.
  bool m_screenGuardRunning;

  // Session id where the current screen guard process was started.
  DWORD m_screenGuardSessionId;

  // Protects m_screenGuardProcess, m_screenGuardRunning,
  // m_screenGuardSessionId and m_screenGuardHeartbeatTimer, which are
  // mutated from the heartbeat timer thread (ensureScreenGuardInCurrentSession)
  // as well as from client-connect/disconnect and config-reload threads
  // (startScreenGuard/stopScreenGuard). A dedicated mutex (rather than
  // m_mutex) keeps its hold time independent of RFB/HTTP/control server
  // restarts. It is reentrant (backed by a Windows critical section), so
  // nested calls from the same thread are safe.
  LocalMutex m_screenGuardMutex;

  // Handle to the shared memory mapping used to communicate with the
  // screen guard process.
  HANDLE m_screenGuardSharedMemory;

  // Pointer to the mapped view of the shared memory block.
  ScreenGuardSharedData *m_screenGuardSharedData;

  // Timer that refreshes the heartbeat in the shared memory block. This
  // is a timer queue timer handle created by CreateTimerQueueTimer().
  HANDLE m_screenGuardHeartbeatTimer;

  // ---------- Control Interface tray icon members ----------

  // Process object of the running tray icon (Control Interface, "-slave"
  // mode) helper. Zero when not running.
  Process *m_trayIconProcess;

  // Session id where the current tray icon process was started.
  DWORD m_trayIconSessionId;

  // Session id and tick count (GetTickCount()) of the last launch attempt,
  // successful or not. Used to avoid respawning every watcher tick in a
  // session where the user already has their own Control Interface open
  // (our "-slave" instance exits right away in that case, see
  // ControlApplication::run()).
  DWORD m_trayIconLastAttemptSessionId;
  ULONG m_trayIconLastAttemptMs;

  // Protects m_trayIconProcess, m_trayIconSessionId and the last-attempt
  // fields above, all mutated from the watcher timer thread.
  LocalMutex m_trayIconMutex;

  // Timer that runs ensureTrayIconInCurrentSession(). Created at server
  // startup when running as a service; independent of the screen guard
  // heartbeat timer, which only runs while the guard is active.
  HANDLE m_trayIconWatcherTimer;
};

#endif
