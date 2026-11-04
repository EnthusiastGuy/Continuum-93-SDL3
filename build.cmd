@echo off
rem Builds Continuum 93 with the Visual Studio toolchain (MSVC + the CMake and Ninja bundled with VS).
rem   build            Release -> build\release\continuum.exe
rem   build debug      Debug   -> build\debug\continuum.exe (console attached)
setlocal EnableDelayedExpansion
set CONFIG=Release
if /i "%~1"=="debug" set CONFIG=Debug
rem Delayed expansion: the "(x86)" in the path would otherwise close the for loop early.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo build: no Visual Studio with the C++ tools found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "DIR=%~dp0build\%CONFIG%"
cmake -S "%~dp0." -B "%DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% >nul || exit /b 1
cmake --build "%DIR%" || exit /b 1
echo build: %DIR%\continuum.exe
