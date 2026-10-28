@echo off
rem FrameLens OSD spike - Tier 2 (uiAccess) test install with a throw-away local certificate.
rem
rem   setup-osd-spike.bat             install  (asks for UAC once)
rem   setup-osd-spike.bat uninstall   remove folder + trusted test certificate
rem   setup-osd-spike.bat status      check, no changes
rem
rem Logic is in setup-osd-spike.ps1 next to this file; read it before running.
setlocal
set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=install"

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"

if not exist "%~dp0setup-osd-spike.ps1" (
    echo setup-osd-spike.ps1 is missing. Keep all spike files in the same folder.
    pause
    exit /b 2
)

"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-osd-spike.ps1" -Action %ACTION%
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
