@echo off
rem ==========================================================================
rem  Diagnostico do ambiente de compilacao.
rem  Execute este script e cole a saida na conversa.
rem
rem  NOTA: escrito sem blocos "if (...)" porque o valor de
rem  %ProgramFiles(x86)% contem parenteses que quebram o parser do cmd.exe.
rem ==========================================================================

set "PF86=%ProgramFiles(x86)%"
set "PF=%ProgramFiles%"
set "WIND=%WINDIR%"

echo === 1. vswhere.exe ===
if not exist "%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" goto :vswhere_pf86_not_found
echo Encontrado em: %PF86%\Microsoft Visual Studio\Installer\vswhere.exe
"%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * -format value
goto :vswhere_pf_check
:vswhere_pf86_not_found
echo NAO encontrado em ProgramFiles (x86)
:vswhere_pf_check
if not exist "%PF%\Microsoft Visual Studio\Installer\vswhere.exe" goto :vswhere_pf_not_found
echo Encontrado em: %PF%\Microsoft Visual Studio\Installer\vswhere.exe
"%PF%\Microsoft Visual Studio\Installer\vswhere.exe" -all -products * -format value
goto :section2
:vswhere_pf_not_found
echo NAO encontrado em ProgramFiles

:section2
echo.
echo === 2. Diretorios do Visual Studio ===
for %%Y in (2022 2019 2017) do call :check_vs_year %%Y
echo.
echo === 3. MSBuild do .NET Framework ===
if not exist "%WIND%\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe" goto :msbuild64_not_found
echo ENCONTRADO: %WIND%\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe
goto :msbuild32_check
:msbuild64_not_found
echo NAO encontrado: Framework64 v4.0.30319
:msbuild32_check
if not exist "%WIND%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe" goto :msbuild32_not_found
echo ENCONTRADO: %WIND%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe
goto :section4
:msbuild32_not_found
echo NAO encontrado: Framework v4.0.30319

:section4
echo.
echo === 4. msbuild no PATH ===
where msbuild 2>nul
if errorlevel 1 echo NAO encontrado no PATH

echo.
echo === 5. ISCC.exe (Inno Setup) ===
if not exist "%PF86%\Inno Setup 6\ISCC.exe" goto :iscc86_not_found
echo ENCONTRADO: %PF86%\Inno Setup 6\ISCC.exe
goto :iscc_pf_check
:iscc86_not_found
echo NAO encontrado em ProgramFiles (x86)
:iscc_pf_check
if not exist "%PF%\Inno Setup 6\ISCC.exe" goto :iscc_pf_not_found
echo ENCONTRADO: %PF%\Inno Setup 6\ISCC.exe
goto :section6
:iscc_pf_not_found
echo NAO encontrado em ProgramFiles

:section6
echo.
echo === 6. Binarios TightVNC ja compilados ===
if not exist "%~dp0Release\x64\tvnserver.exe" goto :bin64_not_found
echo ENCONTRADO: Release\x64\tvnserver.exe
goto :bin32_check
:bin64_not_found
echo NAO encontrado: Release\x64\tvnserver.exe
:bin32_check
if not exist "%~dp0Release\x86\tvnserver.exe" goto :bin32_not_found
echo ENCONTRADO: Release\x86\tvnserver.exe
goto :done
:bin32_not_found
echo NAO encontrado: Release\x86\tvnserver.exe

:done
echo.
echo === Fim do diagnostico ===
exit /b 0

rem ------------------------------------------------------------------
rem Sub-rotina: verifica um ano de Visual Studio.
rem Sem blocos "(...)" porque os caminhos contem parenteses.
rem ------------------------------------------------------------------
:check_vs_year
call :check_one_vs "%PF%\Microsoft Visual Studio\%1\Community"
call :check_one_vs "%PF86%\Microsoft Visual Studio\%1\Community"
call :check_one_vs "%PF%\Microsoft Visual Studio\%1\Professional"
call :check_one_vs "%PF86%\Microsoft Visual Studio\%1\Professional"
call :check_one_vs "%PF%\Microsoft Visual Studio\%1\Enterprise"
call :check_one_vs "%PF86%\Microsoft Visual Studio\%1\Enterprise"
call :check_one_vs "%PF%\Microsoft Visual Studio\%1\BuildTools"
call :check_one_vs "%PF86%\Microsoft Visual Studio\%1\BuildTools"
exit /b 0

rem ------------------------------------------------------------------
rem Sub-rotina: verifica se um diretorio existe (linha unica, sem bloco).
rem ------------------------------------------------------------------
:check_one_vs
if exist "%~1\" echo ENCONTRADO: %~1
exit /b 0
