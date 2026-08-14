@echo off
setlocal

set "MSI_PATH=%~dp0tight.msi"
set "SENHA=stiAlpha@2016"
set "LOG=%TEMP%\tightvnc_install.log"

:: Verifica se esta rodando como administrador
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Este script precisa ser executado como Administrador.
    exit /b 1
)

:: Verifica se o instalador existe na mesma pasta do BAT
if not exist "%MSI_PATH%" (
    echo Instalador nao encontrado em:
    echo %MSI_PATH%
    exit /b 1
)

echo Instalador encontrado:
echo %MSI_PATH%
echo.
echo Instalando TightVNC...

msiexec /i "%MSI_PATH%" /qn /norestart ^
    ADDLOCAL=Server ^
    SERVER_REGISTER_AS_SERVICE=1 ^
    SERVER_ADD_FIREWALL_EXCEPTION=1 ^
    SET_USEVNCAUTHENTICATION=1 ^
    VALUE_OF_USEVNCAUTHENTICATION=1 ^
    SET_PASSWORD=1 ^
    VALUE_OF_PASSWORD="%SENHA%" ^
    SET_USECONTROLAUTHENTICATION=1 ^
    VALUE_OF_USECONTROLAUTHENTICATION=1 ^
    SET_CONTROLPASSWORD=1 ^
    VALUE_OF_CONTROLPASSWORD="%SENHA%" ^
    SET_BLOCKLOCALINPUT=1 ^
    VALUE_OF_BLOCKLOCALINPUT=1 ^
    SET_SCREENGUARDENABLED=1 ^
    VALUE_OF_SCREENGUARDENABLED=1 ^
    /log "%LOG%"

set "MSI_EXIT=%ERRORLEVEL%"

if %MSI_EXIT% EQU 0 (
    echo.
    echo Instalacao concluida com sucesso.
) else (
    echo.
    echo Falha na instalacao. Codigo de erro: %MSI_EXIT%
    echo Verifique o log em:
    echo %LOG%
)

exit /b %MSI_EXIT%
