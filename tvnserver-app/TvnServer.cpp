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

#include "TvnServer.h"
#include "WsConfigRunner.h"
#include "AdditionalActionApplication.h"
#include "ScreenGuardApplication.h"
#include "win-system/CurrentConsoleProcess.h"
#include "win-system/Environment.h"
#include "win-system/WTS.h"

#include "server-config-lib/Configurator.h"

#include "thread/GlobalMutex.h"

#include "tvnserver/resource.h"

#include "wsconfig-lib/TvnLogFilename.h"

#include "network/socket/WindowsSocket.h"

#include "util/StringTable.h"
#include "util/AnsiStringStorage.h"
#include "tvnserver-app/NamingDefs.h"

#include "file-lib/File.h"

// FIXME: Bad dependency on tvncontrol-app.
#include "tvncontrol-app/TransportFactory.h"
#include "tvncontrol-app/ControlPipeName.h"
#include "tvncontrol-app/ControlCommandLine.h"

#include "tvnserver/BuildTime.h"

#include <crtdbg.h>
#include <time.h>
#include <Aclapi.h>
#include "TimeAPI.h"


TvnServer::TvnServer(bool runsInServiceContext,
                     NewConnectionEvents *newConnectionEvents,
                     LogInitListener *logInitListener,
                     Logger *logger)
: Singleton<TvnServer>(),
  ListenerContainer<TvnServerListener *>(),
  m_runAsService(runsInServiceContext),
  m_logInitListener(logInitListener),
  m_rfbClientManager(0),
  m_httpServer(0), m_controlServer(0), m_rfbServer(0),
  m_config(runsInServiceContext),
  m_log(logger),
  m_contextSwitchResolution(1),
  m_extraRfbServers(&m_log),
  m_screenGuardProcess(0),
  m_screenGuardRunning(false),
  m_screenGuardSessionId(0),
  m_screenGuardSharedMemory(0),
  m_screenGuardSharedData(0),
  m_screenGuardHeartbeatTimer(0),
  m_trayIconProcess(0),
  m_trayIconSessionId(0),
  m_trayIconLastAttemptSessionId(0),
  m_trayIconLastAttemptMs(0),
  m_trayIconWatcherTimer(0)
{
  m_log.message(_T("%s Build on %s"),
                 ProductNames::SERVER_PRODUCT_NAME,
                 BuildTime::DATE);

  // Initialize configuration.
  // FIXME: It looks like configurator may be created as a member object.
  Configurator *configurator = Configurator::getInstance();
  configurator->load();
  m_srvConfig = Configurator::getInstance()->getServerConfig();

  try {
    StringStorage logDir;
    m_srvConfig->getLogFileDir(&logDir);
    unsigned char logLevel = m_srvConfig->getLogLevel();
    // FIXME: Use correct log name.
    m_logInitListener->onLogInit(logDir.getString(), LogNames::SERVER_LOG_FILE_STUB_NAME, logLevel);

  } catch (...) {
    // A log error must not be a reason that stop the server.
  }

  // Initialize windows sockets.

  m_log.info(_T("Initialize WinSock"));

  try {
    WindowsSocket::startup(2, 1);
  } catch (Exception &ex) {
    m_log.interror(_T("%s"), ex.getMessage());
  }

  DesktopFactory *desktopFactory = 0;
  if (runsInServiceContext) {
    desktopFactory = &m_serviceDesktopFactory;
  } else {
    desktopFactory = &m_applicationDesktopFactory;
  }

  m_rfbClientManager = new RfbClientManager(0, newConnectionEvents, &m_log, desktopFactory);

  m_rfbClientManager->addListener(this);

  // FIXME: No good to act as a listener before completing the object
  //        construction.
  Configurator::getInstance()->addListener(this);

  {
    // FIXME: Protect only primitive operations.
    // FIXME: Nested lock in protected code (congifuration locking).
    AutoLock l(&m_mutex);

    restartMainRfbServer();
    (void)m_extraRfbServers.reload(m_runAsService, m_rfbClientManager);
    restartHttpServer();
    restartControlServer();
  }

  // Initialize the screen guard shared memory block. The block is created
  // even when no clients are connected; it is cheap and allows the guard
  // process to start at any moment.
  try {
    m_screenGuardSharedMemory =
      CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE,
                        0, sizeof(ScreenGuardSharedData),
                        ScreenGuardSharedNames::SHARED_MEMORY_NAME);
    // Capture this right away: SetSecurityInfo()/MapViewOfFile() below can
    // overwrite the last-error value before we get a chance to read it.
    DWORD screenGuardMemoryCreateError = GetLastError();
    if (m_screenGuardSharedMemory != 0) {
      // Allow all users to open the mapping. This is required when the
      // server runs as a service (session 0) and the screen guard runs
      // in the interactive user session.
      DWORD setSecResult =
        SetSecurityInfo(m_screenGuardSharedMemory, SE_KERNEL_OBJECT,
                        DACL_SECURITY_INFORMATION,
                        0, 0, 0, 0);
      if (setSecResult != ERROR_SUCCESS) {
        m_log.warning(_T("Cannot set security on the screen guard shared")
                      _T(" memory: %d"), (int)setSecResult);
      }
      m_screenGuardSharedData =
        (ScreenGuardSharedData *)MapViewOfFile(m_screenGuardSharedMemory,
                                               FILE_MAP_WRITE, 0, 0, 0);
    }
    if (m_screenGuardSharedData == 0) {
      m_log.error(_T("Failed to map the screen guard shared memory block"));
      if (m_screenGuardSharedMemory != 0) {
        CloseHandle(m_screenGuardSharedMemory);
        m_screenGuardSharedMemory = 0;
      }
    } else if (screenGuardMemoryCreateError == ERROR_ALREADY_EXISTS) {
      // Another tvnserver.exe instance (normally the running Windows
      // service) already owns this shared block. This process is just
      // opening it, e.g. because someone ran tvnserver.exe directly while
      // the service was already active: it must not reset the state the
      // real, running screen guard depends on.
      m_log.info(_T("Screen guard shared memory already existed;")
                _T(" reusing the existing state"));
    } else {
      memset(m_screenGuardSharedData, 0, sizeof(ScreenGuardSharedData));
      m_screenGuardSharedData->serverHeartbeatMs = GetTickCount();
      m_log.info(_T("Screen guard shared memory has been initialized"));
    }
  } catch (...) {
    m_log.error(_T("Failed to initialize the screen guard shared memory"));
  }

  // Keep the Control Interface tray icon available to the local user as
  // soon as they log on, without them having to find and run tvnserver.exe
  // themselves (which would start a second, independent server instance
  // instead of talking to this service).
  if (runsInServiceContext) {
    startTrayIconWatcher();
  }
}

TvnServer::~TvnServer()
{
  // Stop watching for session changes before tearing down the rest of the
  // server; the tray icon process itself is left running, exactly as if
  // the user had opened it manually.
  stopTrayIconWatcher();

  // Stop the screen guard first so the guard process does not outlive the
  // shared memory block.
  stopScreenGuard();

  if (m_screenGuardHeartbeatTimer != 0) {
    DeleteTimerQueueTimer(0, m_screenGuardHeartbeatTimer,
                          INVALID_HANDLE_VALUE);
    m_screenGuardHeartbeatTimer = 0;
  }

  if (m_screenGuardSharedData != 0) {
    UnmapViewOfFile(m_screenGuardSharedData);
    m_screenGuardSharedData = 0;
  }
  if (m_screenGuardSharedMemory != 0) {
    CloseHandle(m_screenGuardSharedMemory);
    m_screenGuardSharedMemory = 0;
  }

  Configurator::getInstance()->removeListener(this);

  stopControlServer();
  stopHttpServer();
  m_extraRfbServers.shutDown();
  stopMainRfbServer();

  ZombieKiller *zombieKiller = ZombieKiller::getInstance();

  // Disconnect all zombies http, rfb, control clients though killing
  // their threads.
  zombieKiller->killAllZombies();

  m_rfbClientManager->removeListener(this);

  delete m_rfbClientManager;

  m_log.info(_T("Shutdown WinSock"));

  try {
    WindowsSocket::cleanup();
  } catch (Exception &ex) {
    m_log.error(_T("%s"), ex.getMessage());
  }
}

// Remark: this method can be called from other threads.
void TvnServer::onConfigReload(ServerConfig *serverConfig)
{
  // Start/stop/restart RFB servers if needed.
  {
    // FIXME: Protect only primitive operations.
    // FIXME: Nested lock in protected code (congifuration locking).
    AutoLock l(&m_mutex);

    bool toggleMainRfbServer =
      m_srvConfig->isAcceptingRfbConnections() != (m_rfbServer != 0);
    bool changeMainRfbPort = m_rfbServer != 0 &&
      (m_srvConfig->getRfbPort() != (int)m_rfbServer->getBindPort());

    const TCHAR *bindHost =
      m_srvConfig->isOnlyLoopbackConnectionsAllowed() ? _T("localhost") : _T("0.0.0.0");
    bool changeBindHost =  m_rfbServer != 0 &&
      _tcscmp(m_rfbServer->getBindHost(), bindHost) != 0;

    if (toggleMainRfbServer ||
        changeMainRfbPort ||
        changeBindHost) {
      restartMainRfbServer();
    }

    // NOTE: ExtraRfbServers::reload() does not throw exceptions if some
    //       servers did not start. However, it returns false in that case.
    //       Here we ignore all errors.
    (void)m_extraRfbServers.reload(m_runAsService, m_rfbClientManager);
  }

  // Start/stop/restart HTTP server if needed.
  {
    // FIXME: Protect only primitive operations.
    AutoLock l(&m_mutex);

    bool toggleHttp =
      m_srvConfig->isAcceptingHttpConnections() != (m_httpServer != 0);
    bool changePort = m_httpServer != 0 &&
      (m_srvConfig->getHttpPort() != (int)m_httpServer->getBindPort());

    if (toggleHttp || changePort) {
      restartHttpServer();
    }
  }

  // Apply the screen guard setting at runtime: when an administrator
  // disables the screen guard while clients are connected, hide it;
  // when it is enabled and clients are connected, show it.
  if (m_srvConfig->isScreenGuardEnabled()) {
    if (!isScreenGuardRunning() && m_rfbClientManager != 0) {
      RfbClientInfoList clientList;
      m_rfbClientManager->getAllClientsInfo(&clientList);
      if (!clientList.empty()) {
        startScreenGuard();
      }
    }
  } else {
    stopScreenGuard();
  }

  changeLogProps();
}

void TvnServer::getServerInfo(TvnServerInfo *info)
{
  bool rfbServerListening = true;
  {
    AutoLock l(&m_mutex);
    rfbServerListening = m_rfbServer != 0;
  }

  StringStorage statusString;

  // Vnc authentication enabled.
  bool vncAuthEnabled = m_srvConfig->isUsingAuthentication();
  // No vnc passwords are set.
  bool noVncPasswords = !m_srvConfig->hasPrimaryPassword() && !m_srvConfig->hasReadOnlyPassword();
  // Determinates that main rfb server cannot accept connection in case of passwords problem.
  bool vncPasswordsError = vncAuthEnabled && noVncPasswords;

  if (rfbServerListening) {
    if (vncPasswordsError) {
      statusString = StringTable::getString(IDS_NO_PASSWORDS_SET);
    } else {
      // FIXME: Usage of deprecated FUNCTION!
      char localAddressString[1024];
      getLocalIPAddrString(localAddressString, 1024);
      AnsiStringStorage ansiString(localAddressString);
      ansiString.toStringStorage(&statusString);

      if (!vncAuthEnabled) {
        statusString.appendString(StringTable::getString(IDS_NO_AUTH_STATUS));
      } // if no auth enabled.
    } // accepting connections and no problem with passwords.
  } else {
    statusString = StringTable::getString(IDS_SERVER_NOT_LISTENING);
  } // not accepting connections.

  UINT stringId = m_runAsService ? IDS_TVNSERVER_SERVICE : IDS_TVNSERVER_APP;

  info->m_statusText.format(_T("%s - %s"),
                            StringTable::getString(stringId),
                            statusString.getString());
  info->m_acceptFlag = rfbServerListening && !vncPasswordsError;
  info->m_serviceFlag = m_runAsService;
}

void TvnServer::generateExternalShutdownSignal()
{
  AutoLock l(&m_listeners);

  vector<TvnServerListener *>::iterator it;
  for (it = m_listeners.begin(); it != m_listeners.end(); it++) {
    TvnServerListener *each = *it;

    each->onTvnServerShutdown();
  } // for all listeners.
}

bool TvnServer::isRunningAsService() const
{
  return m_runAsService;
}

void TvnServer::afterFirstClientConnect()
{
  if (timeBeginPeriod(m_contextSwitchResolution) == TIMERR_NOERROR) {
    m_log.message(_T("Set context switch resolution: %d ms"), m_contextSwitchResolution);
  }
  else {
    m_log.message(_T("Can't change context switch resolution to: %d ms"), m_contextSwitchResolution);
  }

  // Show the visible screen guard to the local user when it is enabled
  // in the server configuration.
  if (m_srvConfig->isScreenGuardEnabled()) {
    startScreenGuard();
  }
}

void TvnServer::afterClientCountChanged()
{
  publishScreenGuardState();
}
void TvnServer::afterLastClientDisconnect()
{
  m_log.message(_T("Restore context switch resolution"));
  timeEndPeriod(m_contextSwitchResolution);

  // Hide the visible screen guard from the local user.
  stopScreenGuard();

  ServerConfig::DisconnectAction action = m_srvConfig->getDisconnectAction();

  // Disconnect action must be executed in process on interactive user session to take effect.
  // Now, choose application keys for specified action.

  StringStorage keys;

  switch (action) {
  case ServerConfig::DA_LOCK_WORKSTATION:
    keys.format(_T("%s"), AdditionalActionApplication::LOCK_WORKSTATION_KEY);
    break;
  case ServerConfig::DA_LOGOUT_WORKSTATION:
    keys.format(_T("%s"), AdditionalActionApplication::LOGOUT_KEY);
    break;
  default:
    return;
  }

  Process *process;

  // Choose how to start process.
  StringStorage thisModulePath;
  Environment::getCurrentModulePath(&thisModulePath);
  thisModulePath.quoteSelf();
  if (isRunningAsService()) {
    bool connectToRdp = m_srvConfig->getConnectToRdpFlag();
    process = new CurrentConsoleProcess(&m_log, connectToRdp, thisModulePath.getString(),
                                        keys.getString());
  } else {
    process = new Process(thisModulePath.getString(), keys.getString());
  }

  m_log.message(_T("Execute disconnect action in separate process"));

  try {
    process->start();
  } catch (SystemException &ex) {
    m_log.error(_T("Failed to start application: \"%s\""), ex.getMessage());
  }

  delete process;
}

void TvnServer::restartHttpServer()
{
  // FIXME: Errors are critical here, they should not be ignored.

  stopHttpServer();

  if (m_srvConfig->isAcceptingHttpConnections()) {
    m_log.message(_T("Starting HTTP server"));
    try {
      // FIXME: HTTP server should bind to localhost if only loopback
      //        connections are allowed.
      m_httpServer = new HttpServer(_T("0.0.0.0"), m_srvConfig->getHttpPort(), m_runAsService, &m_log);
    } catch (Exception &ex) {
      m_log.error(_T("Failed to start HTTP server: \"%s\""), ex.getMessage());
    }
  }
}

void TvnServer::restartControlServer()
{
  // FIXME: Memory leaks.
  // FIXME: Errors are critical here, they should not be ignored.

  stopControlServer();

  m_log.message(_T("Starting control server"));

  try {
    StringStorage pipeName;
    ControlPipeName::createPipeName(isRunningAsService(), &pipeName, &m_log);

    // FIXME: Memory leak
    SecurityAttributes *pipeSecurity = new SecurityAttributes();
    pipeSecurity->setInheritable();
    pipeSecurity->shareToAllUsers();

    const unsigned int maxControlServerPipeBufferSize = 0x10000;
    PipeServer *pipeServer = new PipeServer(pipeName.getString(), maxControlServerPipeBufferSize, pipeSecurity);
    m_controlServer = new ControlServer(pipeServer , m_rfbClientManager, &m_log);
  } catch (Exception &ex) {
    m_log.error(_T("Failed to start control server: \"%s\""), ex.getMessage());
  }
}

void TvnServer::restartMainRfbServer()
{
  // FIXME: Errors are critical here, they should not be ignored.

  stopMainRfbServer();

  if (!m_srvConfig->isAcceptingRfbConnections()) {
    return;
  }

  const TCHAR *bindHost = m_srvConfig->isOnlyLoopbackConnectionsAllowed() ? _T("localhost") : _T("0.0.0.0");
  unsigned short bindPort = m_srvConfig->getRfbPort();

  m_log.message(_T("Starting main RFB server"));

  try {
    m_rfbServer = new RfbServer(bindHost, bindPort, m_rfbClientManager, m_runAsService, &m_log);
  } catch (Exception &ex) {
    m_log.error(_T("Failed to start main RFB server: \"%s\""), ex.getMessage());
  }
}

void TvnServer::stopHttpServer()
{
  m_log.message(_T("Stopping HTTP server"));

  HttpServer *httpServer = 0;
  {
    AutoLock l(&m_mutex);
    httpServer = m_httpServer;
    m_httpServer = 0;
  }
  if (httpServer != 0) {
    delete httpServer;
  }
}

void TvnServer::stopControlServer()
{
  m_log.message(_T("Stopping control server"));

  ControlServer *controlServer = 0;
  {
    AutoLock l(&m_mutex);
    controlServer = m_controlServer;
    m_controlServer = 0;
  }
  if (controlServer != 0) {
    delete controlServer;
  }
}

void TvnServer::stopMainRfbServer()
{
  m_log.message(_T("Stopping main RFB server"));

  RfbServer *rfbServer = 0;
  {
    AutoLock l(&m_mutex);
    rfbServer = m_rfbServer;
    m_rfbServer = 0;
  }
  if (rfbServer != 0) {
    delete rfbServer;
  }
}

void TvnServer::changeLogProps()
{
  StringStorage logDir;
  unsigned char logLevel;
  {
    AutoLock al(&m_mutex);
    m_srvConfig->getLogFileDir(&logDir);
    logLevel = m_srvConfig->getLogLevel();
  }
  m_logInitListener->onChangeLogProps(logDir.getString(), logLevel);
}

// ==========================================================================
// Screen guard implementation.
// ==========================================================================

// How long stopScreenGuard() waits for the guard process to exit on its own
// before terminating it. The guard needs up to 2 s just to notice that the
// client count dropped to zero (4 ticks of its 500 ms timer, see
// ZERO_CLIENTS_EXIT_TICKS in ScreenGuardApplication.cpp) and then still has
// to tear its windows down, so a 2 s budget here means practically every
// stop ends in TerminateProcess even though the guard was about to exit
// cleanly. The wait loop polls every 100 ms and returns as soon as the
// process is gone, so a larger budget costs nothing in the normal case.
static const int SCREEN_GUARD_EXIT_POLLS = 35; // 35 * 100 ms = 3.5 s

// How long ensureScreenGuardInCurrentSession() gives a guard that is being
// abandoned in a no longer active session to hide itself and restore the
// system cursors before it is terminated. One guard timer tick (500 ms) is
// enough; the rest is margin for a busy session.
static const int SCREEN_GUARD_ABANDON_POLLS = 8; // 8 * 100 ms = 800 ms

bool TvnServer::isScreenGuardRunning()
{
  AutoLock l(&m_screenGuardMutex);
  return m_screenGuardProcess != 0 && m_screenGuardRunning;
}

void TvnServer::publishScreenGuardState()
{
  if (m_screenGuardSharedData == 0) {
    return;
  }

  // Collect the current client list. Include clients that authenticated
  // but are still initializing the RFB session, so the guard shows the
  // warning immediately after the authentication phase.
  RfbClientInfoList clientList;
  m_rfbClientManager->getAllClientsInfo(&clientList);

  StringStorage lastClientAddress;
  if (!clientList.empty()) {
    RfbClientInfo &info = clientList.back();
    lastClientAddress = info.m_peerAddr;
  }

  m_log.message(_T("Publishing screen guard state: clientCount=%d,")
               _T(" lastClientAddress=\"%s\", targetSessionId=%u"),
               (int)clientList.size(), lastClientAddress.getString(),
               (unsigned int)getScreenGuardTargetSessionId());

  // Publish under the generation lock protocol: first write the payload,
  // then advance the generation counter.
  _tcsncpy_s(m_screenGuardSharedData->lastClientAddress,
             64,
             lastClientAddress.getString(),
             _TRUNCATE);
  m_screenGuardSharedData->lastClientAddress[63] = _T('\0');
  InterlockedExchange(&m_screenGuardSharedData->clientCount,
                      (LONG)clientList.size());
  InterlockedExchange(&m_screenGuardSharedData->serverHeartbeatMs,
                      (ULONG)GetTickCount());
  InterlockedIncrement(&m_screenGuardSharedData->generation);
}

void TvnServer::refreshScreenGuardHeartbeat()
{
  if (m_screenGuardSharedData != 0) {
    InterlockedExchange(&m_screenGuardSharedData->serverHeartbeatMs,
                        (ULONG)GetTickCount());
  }
  ensureScreenGuardInCurrentSession();
}

void CALLBACK TvnServer::screenGuardHeartbeatTimer(void *lpParameter,
                                                   BOOLEAN timerOrWaitFired)
{
  TvnServer *tvnServer = (TvnServer *)lpParameter;
  if (tvnServer != 0) {
    tvnServer->refreshScreenGuardHeartbeat();
  }
}

DWORD TvnServer::getScreenGuardTargetSessionId()
{
  if (!isRunningAsService()) {
    DWORD processSessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &processSessionId);
    return processSessionId;
  }

  if (isRunningAsService() && m_srvConfig->getConnectToRdpFlag()) {
    DWORD rdpSessionId = WTS::getRdpSessionId(&m_log);
    if (rdpSessionId != 0) {
      return rdpSessionId;
    }
  }
  return WTS::getActiveConsoleSessionId(&m_log);
}

bool TvnServer::launchScreenGuardProcess()
{
  AutoLock l(&m_screenGuardMutex);

  // Make sure there is a valid shared memory block.
  if (m_screenGuardSharedData == 0) {
    m_log.error(_T("Cannot start the screen guard: shared memory is not")
                _T(" initialized"));
    return false;
  }

  StringStorage thisModulePath;
  Environment::getCurrentModulePath(&thisModulePath);
  thisModulePath.quoteSelf();

  StringStorage keys;
  keys.format(_T("%s"), ScreenGuardApplication::SCREEN_GUARD_KEY);

  Process *process = 0;
  try {
    if (isRunningAsService()) {
      bool connectToRdp = m_srvConfig->getConnectToRdpFlag();
      process = new CurrentConsoleProcess(&m_log, connectToRdp,
                                          thisModulePath.getString(),
                                          keys.getString());
    } else {
      process = new Process(thisModulePath.getString(), keys.getString());
    }

    m_log.message(_T("Starting the screen guard application"));

    process->start();

    m_screenGuardProcess = process;
    m_screenGuardRunning = true;
    m_screenGuardSessionId = getScreenGuardTargetSessionId();
  } catch (Exception &e) {
    m_log.error(_T("Failed to start the screen guard application: %s"),
                e.getMessage());
    if (process != 0) {
        delete process;
    }
    return false;
  }

  // Publish the initial state.
  publishScreenGuardState();
  return true;
}

void TvnServer::startScreenGuard()
{
  AutoLock l(&m_screenGuardMutex);

  if (isScreenGuardRunning()) {
    // The guard process is already running. Just publish the current state
    // so it shows the correct client information.
    publishScreenGuardState();
    return;
  }

  if (!launchScreenGuardProcess()) {
    return;
  }

  // Start the heartbeat timer (1 second interval). The timer queue
  // timer does not require a message pump in the calling thread.
  if (m_screenGuardHeartbeatTimer == 0 &&
      !CreateTimerQueueTimer(&m_screenGuardHeartbeatTimer, 0,
                             screenGuardHeartbeatTimer, this,
                             1000, 1000, WT_EXECUTEDEFAULT)) {
    m_log.warning(_T("Failed to start the screen guard heartbeat timer"));
    m_screenGuardHeartbeatTimer = 0;
  }
}

void TvnServer::ensureScreenGuardInCurrentSession()
{
  AutoLock l(&m_screenGuardMutex);

  if (!m_srvConfig->isScreenGuardEnabled() || !isScreenGuardRunning()) {
    return;
  }
  if (m_rfbClientManager == 0) {
    return;
  }

  RfbClientInfoList clientList;
  m_rfbClientManager->getAllClientsInfo(&clientList);
  if (clientList.empty()) {
    return;
  }

  DWORD currentSessionId = getScreenGuardTargetSessionId();
  DWORD waitResult = WaitForSingleObject(
    m_screenGuardProcess->getProcessHandle(), 0);
  if (waitResult != WAIT_OBJECT_0 &&
      currentSessionId == m_screenGuardSessionId) {
    return;
  }

  if (waitResult == WAIT_OBJECT_0) {
    m_log.message(_T("Screen guard process exited; restarting it"));
  } else {
    m_log.message(_T("Screen guard session changed from %u to %u;"
                     " restarting it"),
                  (unsigned int)m_screenGuardSessionId,
                  (unsigned int)currentSessionId);

    // The guard is about to be abandoned in a session that is no longer the
    // active one, but that session may still be alive (fast user switching,
    // a disconnected RDP session). While the guard is visible it replaces
    // every system cursor with a blank one (SetSystemCursor is session wide)
    // and only puts them back from its own cleanup path, so terminating it
    // right away would leave that session without a mouse pointer until the
    // user logs off. Publish "no clients" first: on its next timer tick the
    // guard hides its windows and restores the cursors, and only then is it
    // killed. The real client count is published again by
    // launchScreenGuardProcess() below.
    if (m_screenGuardSharedData != 0) {
      InterlockedExchange(&m_screenGuardSharedData->clientCount, 0);
      InterlockedIncrement(&m_screenGuardSharedData->generation);
    }

    bool guardExited = false;
    for (int i = 0; i < SCREEN_GUARD_ABANDON_POLLS; i++) {
      if (WaitForSingleObject(m_screenGuardProcess->getProcessHandle(), 0) ==
          WAIT_OBJECT_0) {
        guardExited = true;
        break;
      }
      Sleep(100);
    }

    if (!guardExited) {
      try {
        m_screenGuardProcess->kill();
      } catch (Exception &e) {
        m_log.error(_T("Failed to terminate stale screen guard: %s"),
                    e.getMessage());
      }
    }
  }

  delete m_screenGuardProcess;
  m_screenGuardProcess = 0;
  m_screenGuardRunning = false;
  m_screenGuardSessionId = 0;

  launchScreenGuardProcess();
}

void TvnServer::stopScreenGuard()
{
  // Detach and delete the heartbeat timer *before* taking
  // m_screenGuardMutex. DeleteTimerQueueTimer(..., INVALID_HANDLE_VALUE)
  // blocks until any in-flight callback finishes, and that callback
  // (refreshScreenGuardHeartbeat -> ensureScreenGuardInCurrentSession)
  // needs the same mutex to finish; waiting for it while holding the
  // mutex would deadlock.
  HANDLE heartbeatTimer = 0;
  {
    AutoLock l(&m_screenGuardMutex);
    heartbeatTimer = m_screenGuardHeartbeatTimer;
    m_screenGuardHeartbeatTimer = 0;
  }
  if (heartbeatTimer != 0) {
    DeleteTimerQueueTimer(0, heartbeatTimer, INVALID_HANDLE_VALUE);
  }

  // Held for the rest of this function, including the up-to-2s wait loop
  // below: this serializes stop against a concurrent startScreenGuard()/
  // ensureScreenGuardInCurrentSession() on another thread (e.g. a client
  // reconnecting while the previous one's guard is still shutting down),
  // which may then block for that long. That is an accepted trade-off:
  // holding the lock only around individual field reads/writes would let
  // two threads race to launch a second guard process (the original bug
  // this mutex fixes), which is worse than a bounded UI-visibility delay.
  AutoLock l(&m_screenGuardMutex);

  if (!isScreenGuardRunning()) {
    m_screenGuardRunning = false;
    if (m_screenGuardProcess != 0) {
      delete m_screenGuardProcess;
      m_screenGuardProcess = 0;
    }
    m_screenGuardSessionId = 0;
    return;
  }

  // Publish zero clients and give the guard process a short grace period
  // to terminate itself cleanly.
  if (m_screenGuardSharedData != 0) {
    InterlockedExchange(&m_screenGuardSharedData->clientCount, 0);
    InterlockedIncrement(&m_screenGuardSharedData->generation);
  }

  m_log.message(_T("Stopping the screen guard application"));

  for (int i = 0; i < SCREEN_GUARD_EXIT_POLLS; i++) {
    if (!m_screenGuardRunning) {
      break;
    }
    Sleep(100);
    // Note: the process exit is detected by the guard process itself;
    // here we just wait for the process object to become signaled.
    DWORD waitResult = WaitForSingleObject(
      m_screenGuardProcess->getProcessHandle(), 0);
    if (waitResult == WAIT_OBJECT_0) {
      m_screenGuardRunning = false;
    }
  }

  if (m_screenGuardRunning) {
    // The guard did not exit in time. Terminate it.
    m_log.warning(_T("The screen guard application did not exit in time,")
                  _T(" terminating it"));
    try {
      m_screenGuardProcess->kill();
    } catch (Exception &e) {
      m_log.error(_T("Failed to terminate the screen guard: %s"),
                  e.getMessage());
    }
    m_screenGuardRunning = false;
  }

  delete m_screenGuardProcess;
  m_screenGuardProcess = 0;
  m_screenGuardSessionId = 0;
}

// ==========================================================================
// Control Interface tray icon session follower.
// ==========================================================================

// Minimum time between two launch attempts in the same session. Avoids
// respawning a process every watcher tick when the user already has their
// own Control Interface open in that session: our "-slave" instance then
// exits almost immediately because of its own per-session single-instance
// check (see ControlApplication::run()), which would otherwise look like a
// stuck/exited process on every single tick.
static const ULONG TRAY_ICON_RETRY_INTERVAL_MS = 30000; // 30 s

void TvnServer::startTrayIconWatcher()
{
  if (m_trayIconWatcherTimer == 0 &&
      !CreateTimerQueueTimer(&m_trayIconWatcherTimer, 0,
                             trayIconWatcherTimer, this,
                             0, 1000, WT_EXECUTEDEFAULT)) {
    m_log.warning(_T("Failed to start the tray icon watcher timer"));
    m_trayIconWatcherTimer = 0;
  }
}

void TvnServer::stopTrayIconWatcher()
{
  HANDLE watcherTimer = 0;
  {
    AutoLock l(&m_trayIconMutex);
    watcherTimer = m_trayIconWatcherTimer;
    m_trayIconWatcherTimer = 0;
  }
  if (watcherTimer != 0) {
    // Waits for an in-flight callback to finish; must not be called while
    // holding m_trayIconMutex (same deadlock concern as the screen guard
    // heartbeat timer, see stopScreenGuard()).
    DeleteTimerQueueTimer(0, watcherTimer, INVALID_HANDLE_VALUE);
  }

  AutoLock l(&m_trayIconMutex);
  if (m_trayIconProcess != 0) {
    delete m_trayIconProcess;
    m_trayIconProcess = 0;
  }
  m_trayIconSessionId = 0;
}

void CALLBACK TvnServer::trayIconWatcherTimer(void *lpParameter,
                                              BOOLEAN timerOrWaitFired)
{
  TvnServer *tvnServer = (TvnServer *)lpParameter;
  if (tvnServer != 0) {
    tvnServer->ensureTrayIconInCurrentSession();
  }
}

void TvnServer::ensureTrayIconInCurrentSession()
{
  AutoLock l(&m_trayIconMutex);

  if (!isRunningAsService()) {
    // Application mode already shows its own tray icon; nothing to do.
    return;
  }

  DWORD currentSessionId = getScreenGuardTargetSessionId();

  if (m_trayIconProcess != 0) {
    bool sessionChanged = currentSessionId != m_trayIconSessionId;
    bool processExited = WaitForSingleObject(
      m_trayIconProcess->getProcessHandle(), 0) == WAIT_OBJECT_0;
    if (!sessionChanged && !processExited) {
      // Still running in the right session; nothing to do.
      return;
    }
    if (sessionChanged) {
      m_log.message(_T("Tray icon session changed from %u to %u;")
                    _T(" restarting it"),
                    (unsigned int)m_trayIconSessionId,
                    (unsigned int)currentSessionId);
    }
    delete m_trayIconProcess;
    m_trayIconProcess = 0;
    m_trayIconSessionId = 0;
  }

  if (currentSessionId == m_trayIconLastAttemptSessionId &&
      (GetTickCount() - m_trayIconLastAttemptMs) < TRAY_ICON_RETRY_INTERVAL_MS) {
    return;
  }
  m_trayIconLastAttemptSessionId = currentSessionId;
  m_trayIconLastAttemptMs = GetTickCount();

  StringStorage thisModulePath;
  Environment::getCurrentModulePath(&thisModulePath);
  thisModulePath.quoteSelf();

  StringStorage keys;
  keys.format(_T("%s %s"), ControlCommandLine::CONTROL_SERVICE,
             ControlCommandLine::SLAVE_MODE);

  try {
    bool connectToRdp = m_srvConfig->getConnectToRdpFlag();
    Process *process = new CurrentConsoleProcess(&m_log, connectToRdp,
                                                 thisModulePath.getString(),
                                                 keys.getString());
    process->start();
    m_trayIconProcess = process;
    m_trayIconSessionId = currentSessionId;
    m_log.message(_T("Started the Control Interface tray icon in session %u"),
                 (unsigned int)currentSessionId);
  } catch (Exception &e) {
    m_log.error(_T("Failed to start the Control Interface tray icon: %s"),
               e.getMessage());
  }
}
