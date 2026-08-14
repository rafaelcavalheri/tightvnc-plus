@echo off
rem ==========================================================================
rem  Build script for TightVNC Server with the Screen Guard feature.
rem
rem  Usage:
rem    build-tvnserver.bat            Builds the Release x64 configuration.
rem    build-tvnserver.bat x86        Builds the Release Win32 configuration.
rem    build-tvnserver.bat x64        Builds the Release x64 configuration.
rem
rem  The script locates Visual Studio automatically:
rem    1) via vswhere.exe (VS 2017+),
rem    2) via the well-known installation folders of VS 2017/2019/2022
rem       (Community, Professional, Enterprise, BuildTools),
rem    3) via the "msbuild" command on the PATH (e.g. inside a
rem       "Developer Command Prompt").
rem
rem  NOTA: escrito sem blocos "if (...)" porque o valor de
rem  %ProgramFiles(x86)% contem parenteses que quebram o parser do cmd.exe.
rem ==========================================================================

setlocal

set PLATFORM=%1
if "%PLATFORM%"=="" set PLATFORM=x64
if not "%PLATFORM%"=="x64" if not "%PLATFORM%"=="x86" if not "%PLATFORM%"=="Win32" (
  echo Error: unknown platform "%PLATFORM%". Use x86 or x64.
  exit /b 1
)
if "%PLATFORM%"=="x86" set PLATFORM=Win32

set SOLUTION=%~dp0tightvnc2019.sln

rem Resolve environment variables (their values contain parentheses, so we
rem must avoid using them inside "(...)" blocks later in this script).
set "PF86=%ProgramFiles(x86)%"
set "PF=%ProgramFiles%"
set "WIND=%WINDIR%"

set VSWHERE=
if not exist "%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" goto :vswhere_pf_check
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
goto :vswhere_done
:vswhere_pf_check
if not exist "%PF%\Microsoft Visual Studio\Installer\vswhere.exe" goto :vswhere_done
set "VSWHERE=%PF%\Microsoft Visual Studio\Installer\vswhere.exe"
:vswhere_done

set MSBUILD=
set VCVARSALL=

if not defined VSWHERE goto :no_vswhere

rem First attempt: require the MSBuild component.
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2^>nul`) do (
  if not defined MSBUILD set "MSBUILD=%%i"
)
rem Second attempt (older vswhere): without the component requirement.
if defined MSBUILD goto :vswhere_vcvars
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -find MSBuild\**\Bin\MSBuild.exe 2^>nul`) do (
  if not defined MSBUILD set "MSBUILD=%%i"
)
:vswhere_vcvars
rem Locate vcvarsall.bat for environment setup.
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -find VC\Auxiliary\Build\vcvarsall.bat 2^>nul`) do (
  if not defined VCVARSALL set "VCVARSALL=%%i"
)

:no_vswhere

rem ------------------------------------------------------------------
rem Well-known Visual Studio installation folders (no (...) blocks:
rem the %PF%/%PF86% values contain parentheses).
rem ------------------------------------------------------------------
if defined MSBUILD goto :vcvars_fallback
for %%Y in (2022 2019 2017) do (
  for %%E in (Community Professional Enterprise BuildTools) do call :check_msbuild_folder %%Y %%E
)
:vcvars_fallback
if defined VCVARSALL goto :netfx_fallback
for %%Y in (2022 2019 2017) do (
  for %%E in (Community Professional Enterprise BuildTools) do call :check_vcvars_folder %%Y %%E
)

:netfx_fallback
rem ------------------------------------------------------------------
rem .NET Framework MSBuild (VS 2015 and older, or as last resort)
rem ------------------------------------------------------------------
if defined MSBUILD goto :check_found
if not exist "%WIND%\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe" goto :netfx32_check
set "MSBUILD=%WIND%\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe"
:netfx32_check
if defined MSBUILD goto :check_found
if not exist "%WIND%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe" goto :check_found
set "MSBUILD=%WIND%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe"

:check_found
if defined MSBUILD goto :configure_env
if defined VCVARSALL goto :configure_env
echo.
echo ERRO: Visual Studio / MSBuild nao foi encontrado nesta maquina.
echo.
echo Opcoes:
echo   a^) Instale o "Visual Studio 2022 Build Tools" com a carga
echo      "Desenvolvimento para desktop com C++":
echo        https://visualstudio.microsoft.com/pt-br/downloads/
echo   b^) Ou abra o "Developer Command Prompt for VS 2022" no menu
echo      Iniciar e execute este script a partir dele.
echo.
exit /b 1

rem ------------------------------------------------------------------
rem Configure the build environment (cl.exe, headers, libs)
rem ------------------------------------------------------------------
:configure_env
if not defined VCVARSALL goto :run_msbuild
echo Configurando o ambiente de compilacao: "%VCVARSALL%"
if /i "%PLATFORM%"=="x64" goto :vcvars_x64
call "%VCVARSALL%" x86
goto :vcvars_done
:vcvars_x64
call "%VCVARSALL%" x64
:vcvars_done
if errorlevel 1 (
  echo.
  echo ERRO: falha ao configurar o ambiente com vcvarsall.bat.
  exit /b 1
)

:run_msbuild
if not defined MSBUILD set "MSBUILD=msbuild"

echo.
echo Building %SOLUTION%  [Release ^| %PLATFORM%]
echo Using MSBuild: %MSBUILD%
if defined VCVARSALL echo Using VcVarsAll: %VCVARSALL%
echo.

rem The solution targets the v140_xp toolset (VS 2015 + Windows XP support)
rem which is not installed. Override it with the current toolset (v143).
"%MSBUILD%" "%SOLUTION%" /m /t:Rebuild /p:Configuration=Release /p:Platform=%PLATFORM% /p:PlatformToolset=v143 /v:minimal /nologo
if errorlevel 1 (
  echo.
  echo BUILD FAILED.
  exit /b 1
)

echo.
echo BUILD SUCCEEDED.
if /i "%PLATFORM%"=="x64" (
  echo Output: %~dp0Release\x64\tvnserver.exe
) else (
  echo Output: %~dp0Release\x86\tvnserver.exe
)
exit /b 0

rem ------------------------------------------------------------------
rem Sub-rotinas (chamadas via "call" para evitar blocos com parenteses)
rem ------------------------------------------------------------------
:check_msbuild_folder
if defined MSBUILD exit /b 0
if not exist "%PF%\Microsoft Visual Studio\%1\%2\MSBuild\Current\Bin\MSBuild.exe" goto :check_msbuild_pf86
set "MSBUILD=%PF%\Microsoft Visual Studio\%1\%2\MSBuild\Current\Bin\MSBuild.exe"
exit /b 0
:check_msbuild_pf86
if not exist "%PF86%\Microsoft Visual Studio\%1\%2\MSBuild\Current\Bin\MSBuild.exe" exit /b 0
set "MSBUILD=%PF86%\Microsoft Visual Studio\%1\%2\MSBuild\Current\Bin\MSBuild.exe"
exit /b 0

:check_vcvars_folder
if defined VCVARSALL exit /b 0
if not exist "%PF%\Microsoft Visual Studio\%1\%2\VC\Auxiliary\Build\vcvarsall.bat" goto :check_vcvars_pf86
set "VCVARSALL=%PF%\Microsoft Visual Studio\%1\%2\VC\Auxiliary\Build\vcvarsall.bat"
exit /b 0
:check_vcvars_pf86
if not exist "%PF86%\Microsoft Visual Studio\%1\%2\VC\Auxiliary\Build\vcvarsall.bat" exit /b 0
set "VCVARSALL=%PF86%\Microsoft Visual Studio\%1\%2\VC\Auxiliary\Build\vcvarsall.bat"
exit /b 0
