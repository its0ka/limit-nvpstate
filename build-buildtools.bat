@echo off
rem Builds limit-nvpstate with the free "Build Tools for Visual Studio" (no IDE needed).
rem usage: build-buildtools.bat C:\Qt\5.14.2\msvc2017_64
setlocal

if "%~1"=="" (
    echo usage: %~nx0 ^<path to the Qt kit, e.g. C:\Qt\5.14.2\msvc2017_64^>
    exit /b 1
)

set "QT_DIR=%~1"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo error: Build Tools for Visual Studio are not installed
    exit /b 1
)

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo error: the "C++ build tools" workload was not found in the Visual Studio installation
    exit /b 1
)

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" || exit /b 1
cmake --build build || exit /b 1

echo.
echo Done: %~dp0build\limit-nvpstate\limit-nvpstate.exe
