@echo off
rem Packages Continuum 93: builds Windows, Linux x64/arm64 and web (build.cmd all), then zips them into dist\
rem and assembles the site in dist\web\ (see package.sh, which does the zipping in Docker).
rem   package              build, then package
rem   package --no-build   package what is already built
setlocal
if /i not "%~1"=="--no-build" (call "%~dp0build.cmd" all || exit /b 1)
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
docker run --rm -v "%ROOT%:/src" -w /src emscripten/emsdk:6.0.13 sh package.sh --inside || exit /b 1
