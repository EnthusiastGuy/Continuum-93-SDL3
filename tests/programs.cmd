@echo off
rem Program vectors end to end: the reference compiles and runs every program under tests\programs,
rem then the C++ build replays them. Each side gets a fresh, identical guest file system, because
rem the fs tests save files (zz_*) and there is no delete interrupt.
setlocal
set "O=%~dp0oracle\bin\Release\net8.0"
set "B=%~dp0..\build"
set "FS=%B%\fs-cpp"
for %%d in ("%O%\Data\filesystem" "%O%\Data\filesystem\programs" "%O%\Data\filesystem\programs\oldOS") do del /q "%%~d\zz_*" 2>nul
dotnet "%O%\oracle.dll" --programs "%~dp0programs" "%B%\programs.txt" || exit /b 1
for %%d in ("%O%\Data\filesystem" "%O%\Data\filesystem\programs" "%O%\Data\filesystem\programs\oldOS") do del /q "%%~d\zz_*" 2>nul
if exist "%FS%" rmdir /s /q "%FS%"
robocopy "%O%\Data\filesystem" "%FS%" /e /nfl /ndl /njh /njs >nul
rem Build interrupts may read ..\os (the boot selector builds Halcyon from there, D-19)
robocopy "%O%\Data\os" "%B%\os" /e /nfl /ndl /njh /njs >nul
set "C93_FS=%FS%"
"%B%\release\conformance.exe" --programs "%B%\programs.txt"
