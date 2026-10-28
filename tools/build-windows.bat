@echo off
rem FrameLens - build with Visual Studio (MSVC) on this PC.
rem Writes everything to build.log and the outcome to build-status.txt next to this repo.
setlocal EnableExtensions
cd /d "%~dp0.."
set "FL_ROOT=%CD%"
set "FL_LOG=%FL_ROOT%\build.log"
set "FL_STATUS=%FL_ROOT%\build-status.txt"
> "%FL_LOG%" echo FrameLens build %DATE% %TIME%
> "%FL_STATUS%" echo RUNNING

set "FL_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%FL_VSWHERE%" (
    >> "%FL_LOG%" echo vswhere.exe not found: no Visual Studio / Build Tools installed.
    > "%FL_STATUS%" echo NO_VISUAL_STUDIO
    goto :end
)

set "FL_VSPATH="
for /f "usebackq tokens=*" %%i in (`"%FL_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FL_VSPATH=%%i"
>> "%FL_LOG%" echo Visual Studio with C++ tools: "%FL_VSPATH%"
"%FL_VSWHERE%" -all -products * -format text >> "%FL_LOG%" 2>&1
if not defined FL_VSPATH (
    >> "%FL_LOG%" echo Visual Studio found, but without the C++ x64 tools workload.
    > "%FL_STATUS%" echo NO_CPP_WORKLOAD
    goto :end
)

rem vcvars output goes to its own file: a helper it starts can keep that file handle
rem open, which would block every later write to build.log.
call "%FL_VSPATH%\VC\Auxiliary\Build\vcvars64.bat" > "%FL_ROOT%\build-vcvars.log" 2>&1
if errorlevel 1 (
    > "%FL_STATUS%" echo VCVARS_FAILED
    goto :end
)

where cmake >> "%FL_LOG%" 2>&1
if errorlevel 1 (
    >> "%FL_LOG%" echo cmake not on PATH - the "C++ CMake tools for Windows" component is missing.
    > "%FL_STATUS%" echo NO_CMAKE
    goto :end
)
set "FL_GEN=Ninja"
where ninja >> "%FL_LOG%" 2>&1 || set "FL_GEN=Visual Studio 18 2026"
>> "%FL_LOG%" echo Generator: %FL_GEN%

>> "%FL_LOG%" echo ===== configure =====
if "%FL_GEN%"=="Ninja" (
    cmake -S "%FL_ROOT%" -B "%FL_ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release >> "%FL_LOG%" 2>&1
) else (
    cmake -S "%FL_ROOT%" -B "%FL_ROOT%\build" -G "%FL_GEN%" -A x64 >> "%FL_LOG%" 2>&1
)
if errorlevel 1 (
    > "%FL_STATUS%" echo CONFIGURE_FAILED
    goto :end
)

>> "%FL_LOG%" echo ===== build =====
cmake --build "%FL_ROOT%\build" --config Release >> "%FL_LOG%" 2>&1
if errorlevel 1 (
    > "%FL_STATUS%" echo BUILD_FAILED
    goto :end
)

>> "%FL_LOG%" echo ===== tests =====
ctest --test-dir "%FL_ROOT%\build" -C Release --output-on-failure >> "%FL_LOG%" 2>&1
if errorlevel 1 (
    > "%FL_STATUS%" echo TESTS_FAILED
    goto :end
)

> "%FL_STATUS%" echo OK
:end
>> "%FL_LOG%" echo Finished %DATE% %TIME%
type "%FL_STATUS%"
endlocal
