@echo off
rem FrameLens one-time setup: lets your account start ETW sessions for FPS capture
rem by adding it to the built-in "Performance Log Users" group (SID S-1-5-32-559).
rem
rem   setup-etw-access.bat           add your account (asks for UAC once)
rem   setup-etw-access.bat remove    undo
rem   setup-etw-access.bat status    check, no changes
rem
rem The actual logic is in setup-etw-access.ps1 next to this file; read it before running.
setlocal
set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=add"

if not exist "%~dp0setup-etw-access.ps1" (
    echo setup-etw-access.ps1 is missing. Keep both files in the same folder.
    pause
    exit /b 2
)

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-etw-access.ps1" -Action %ACTION%
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
