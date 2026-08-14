@echo off
rem ==========================================================================
rem  Build completo + empacotamento com Inno Setup e WiX (MSI).
rem
rem  Fluxo:
rem    1) Compila a solucao tightvnc2019.sln (Release x64 e Win32).
rem    2) Gera o instalador com o Inno Setup (ISCC.exe).
rem    3) Gera os instaladores MSI com o WiX Toolset (candle.exe/light.exe).
rem
rem  Uso (a partir da pasta tightvnc):
rem    build-and-package.bat
rem
rem  Saida final, tudo em tightvnc\Output\:
rem    tightvnc-2.8.81-setup.exe   (Inno Setup)
rem    tight.msi                   (MSI x64, nome usado pelo script de deploy)
rem    tightvnc-2.8.81-x86.msi     (MSI x86)
rem ==========================================================================

setlocal
set ROOT=%~dp0

echo.
echo ============================================================
echo  ETAPA 1/3: Compilando TightVNC (Release x64 e Win32)...
echo ============================================================
call "%ROOT%build-tvnserver.bat" x64
if errorlevel 1 goto :failed
call "%ROOT%build-tvnserver.bat" x86
if errorlevel 1 goto :failed

echo.
echo ============================================================
echo  ETAPA 2/3: Gerando instalador com Inno Setup...
echo ============================================================

rem --- Localiza o ISCC.exe do Inno Setup ------------------------
rem NOTA: sem blocos "if (...)" porque o valor de %ProgramFiles(x86)%
rem contem parenteses que quebram o parser do cmd.exe.
set "PF86=%ProgramFiles(x86)%"
set "PF=%ProgramFiles%"
set ISCC=
if not exist "%PF86%\Inno Setup 6\ISCC.exe" goto :iscc_pf_check
set "ISCC=%PF86%\Inno Setup 6\ISCC.exe"
goto :iscc_found
:iscc_pf_check
if not exist "%PF%\Inno Setup 6\ISCC.exe" goto :iscc_not_found
set "ISCC=%PF%\Inno Setup 6\ISCC.exe"
:iscc_found
echo Usando: %ISCC%
"%ISCC%" "%ROOT%tightvnc-setup.iss"
if errorlevel 1 goto :failed

goto :msi_step

:iscc_not_found

echo.
echo ERRO: Inno Setup 6 (ISCC.exe) nao encontrado.
echo Instale o Inno Setup 6 e tente novamente, ou execute:
echo     ISCC.exe "%ROOT%tightvnc-setup.iss"
echo.
exit /b 1

:msi_step

echo.
echo ============================================================
echo  ETAPA 3/3: Gerando instaladores MSI (WiX Toolset)...
echo ============================================================

set WIXBIN=
if not exist "%PF86%\WiX Toolset v3.14\bin\candle.exe" goto :wix_check2
set "WIXBIN=%PF86%\WiX Toolset v3.14\bin"
goto :wix_found
:wix_check2
if not exist "%PF%\WiX Toolset v3.14\bin\candle.exe" goto :wix_check3
set "WIXBIN=%PF%\WiX Toolset v3.14\bin"
goto :wix_found
:wix_check3
if not exist "%PF86%\WiX Toolset v3.11\bin\candle.exe" goto :wix_not_found
set "WIXBIN=%PF86%\WiX Toolset v3.11\bin"
:wix_found
if not defined WIXBIN goto :wix_not_found

echo Usando WiX: %WIXBIN%
set "PATH=%WIXBIN%;%PATH%"

rem Bump a persisted counter so every build gets a strictly increasing MSI
rem version (2.8.<n>). Reinstalling an MSI with the same version as one
rem already on the machine is silently blocked (see product_definitions.wxi).
set "REVFILE=%ROOT%wix-installer\.build_revision"
set BUILDREV=81
if exist "%REVFILE%" set /p BUILDREV=<"%REVFILE%"
set /a BUILDREV=BUILDREV+1
echo %BUILDREV%>"%REVFILE%"
echo Versao do MSI: 2.8.%BUILDREV%

pushd "%ROOT%wix-installer"
call .\make-msi.bat tightvnc-2.8.81-x86 tightvnc-2.8.81-x64 "" "BuildRevision=%BUILDREV%"
set MSI_RESULT=%ERRORLEVEL%
popd
if not "%MSI_RESULT%"=="0" goto :failed

rem NOTA: make-msi.bat grava o x86 em ..\Release\ (raiz) e o x64 em
rem ..\x64\Release\ -- esses caminhos sao definidos dentro do proprio
rem make-msi.bat (OUT_DIR), independente do BinFolder usado para ler os
rem binarios compilados.
if not exist "%ROOT%Output" mkdir "%ROOT%Output"
copy /Y "%ROOT%x64\Release\tightvnc-2.8.81-x64.msi" "%ROOT%Output\tight.msi" >nul
copy /Y "%ROOT%Release\tightvnc-2.8.81-x86.msi" "%ROOT%Output\tightvnc-2.8.81-x86.msi" >nul

goto :done

:wix_not_found

echo.
echo AVISO: WiX Toolset v3.x (candle.exe) nao encontrado. Pulando geracao do MSI.
echo Instale com: winget install --id WiXToolset.WiXToolset
echo (depende do recurso do Windows ".NET Framework 3.5 - NetFx3")
echo.
goto :done

:done

echo.
echo ============================================================
echo  CONCLUIDO. Instaladores gerados em %ROOT%Output\
echo ============================================================
exit /b 0

:failed
echo.
echo *** FALHA NO PROCESSO. ***
echo *** Execute diagnose-env.bat e cole a saida na conversa. ***
exit /b 1
