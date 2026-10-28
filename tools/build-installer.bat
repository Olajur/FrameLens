@echo off
rem FrameLens - build the app (tools\build-windows.bat), then the setup exe with Inno Setup 6.
rem Output: build\installer\FrameLens-<version>-setup.exe
setlocal EnableExtensions
cd /d "%~dp0.."

call "%~dp0build-windows.bat"
set /p FL_RESULT=<build-status.txt
if not "%FL_RESULT%"=="OK" (
    echo App build did not succeed ^(%FL_RESULT%^) - see build.log. Installer not built.
    exit /b 1
)

set "FL_ISCC="
for %%p in ("%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe") do (
    if not defined FL_ISCC if exist %%p set "FL_ISCC=%%~p"
)
if not defined FL_ISCC (
    echo Inno Setup 6 not found. Install it with:  winget install --id JRSoftware.InnoSetup --exact --scope user
    exit /b 1
)

"%FL_ISCC%" /Q "installer\framelens.iss"
if errorlevel 1 (
    echo Installer build failed.
    exit /b 1
)
echo Installer: %CD%\build\installer
dir /b build\installer\*.exe
endlocal
