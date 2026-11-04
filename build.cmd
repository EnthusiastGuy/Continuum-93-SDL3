@echo off
rem Builds Continuum 93 with the Visual Studio toolchain (MSVC + the CMake and Ninja bundled with VS).
rem   build            Release -> build\release\continuum.exe
rem   build debug      Debug   -> build\debug\continuum.exe (console attached)
rem   build linux      Docker  -> build\linux-x64, build\linux-arm64 (glibc 2.36+: Raspberry Pi OS bookworm too)
rem   build web        Docker  -> build\web (serve with COOP/COEP headers, docs\WEB.md)
rem   build all        release + linux + web
rem   build boot [x64^|x86^|rpi]  Docker -> bootable images (PCs, Raspberry Pi), dist\continuum93-boot-*.zip (docs\BOOT.md)
setlocal EnableDelayedExpansion
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
if /i "%~1"=="linux" (call :linux & exit /b !errorlevel!)
if /i "%~1"=="boot" (call :boot %2 & exit /b !errorlevel!)
if /i "%~1"=="web" (call :web & exit /b !errorlevel!)
set CONFIG=Release
if /i "%~1"=="debug" set CONFIG=Debug
call :native || exit /b 1
if /i "%~1"=="all" (
    call :linux || exit /b 1
    call :web || exit /b 1
)
exit /b 0

:native
rem Delayed expansion: the "(x86)" in the path would otherwise close the for loop early.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo build: no Visual Studio with the C++ tools found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "DIR=%ROOT%\build\%CONFIG%"
cmake -S "%ROOT%" -B "%DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% >nul || exit /b 1
cmake --build "%DIR%" || exit /b 1
echo build: %DIR%\continuum.exe
exit /b 0

:linux
rem arm64 runs emulated (QEMU, built into Docker Desktop): slow the first time, no cross toolchain to keep.
for %%p in (amd64 arm64) do (
    set "N=%%p"
    if "%%p"=="amd64" set "N=x64"
    docker build -q --platform linux/%%p -t continuum93-linux:%%p -f "%ROOT%\docker\linux.Dockerfile" "%ROOT%\docker" >nul || exit /b 1
    call :docked continuum93-linux:%%p build/linux-!N! "cmake -S . -B build/linux-!N! -G Ninja -DCMAKE_BUILD_TYPE=Release" "--platform linux/%%p" || exit /b 1
)
exit /b 0

:web
rem The named volume keeps Emscripten's compiled system libraries between runs.
call :docked emscripten/emsdk:6.0.13 build/web "emcmake cmake -S . -B build/web -DCMAKE_BUILD_TYPE=Release" "-v continuum93-emcache:/emsdk/upstream/emscripten/cache"
exit /b %errorlevel%

:boot
rem Each arch builds in its own container (x86 runs natively as linux/386); the volume keeps the kernel trees.
rem The Pi: its userland in an emulated arm64 container, then its kernel and image cross-built on amd64.
for %%a in (amd64:x64:all 386:x86:all arm64:rpi:userland amd64:rpi:image) do for /f "tokens=1-3 delims=:" %%p in ("%%a") do (
    if "%~1"=="" (set "GO=1") else (set "GO=" & if /i "%~1"=="%%q" set "GO=1")
    if defined GO (
        docker build -q --platform linux/%%p -t continuum93-boot:%%p "%ROOT%\boot" >nul || exit /b 1
        docker run --rm --platform linux/%%p -v "%ROOT%:/src" -v continuum93-boot:/cache continuum93-boot:%%p sh /src/boot/build.sh %%q %%r || exit /b 1
    )
)
exit /b 0

rem :docked <image> <build dir> <configure command> <docker args>
:docked
docker run --rm %~4 -v "%ROOT%:/src" -w /src %1 sh -c "%~3 >/dev/null && cmake --build %2 --parallel" || exit /b 1
echo build: %ROOT%\%2
exit /b 0
