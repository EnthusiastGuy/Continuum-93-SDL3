@echo off
rem -png2font end to end: the reference (oracle --png2font: its own FontConvertor and MonoGame texture
rem loading) and the C++ build convert every PNG in Data\fonts, Data\filesystem\fonts and
rem tests\png2font (crafted: odd cell sizes, ragged sheets, cells wider than 8, empty glyphs, alpha-0
rem colours, RGB/grey/palette+tRNS). Every .font must be byte-identical, and so must the reference's
rem .font files committed next to their PNGs in Data\fonts.
setlocal enabledelayedexpansion
set "O=%~dp0oracle\bin\Release\net8.0\oracle.dll"
set "E=%~dp0..\build\release\continuum.exe"
set "T=%~dp0..\build\png2font"
if exist "%T%" rmdir /s /q "%T%"
mkdir "%T%\ref" "%T%\cpp"
for %%d in ("%~dp0..\Data\fonts" "%~dp0..\Data\filesystem\fonts" "%~dp0png2font") do copy /y "%%~d\*.png" "%T%\cpp" >nul
rem Relative paths: the whole list must fit cmd.exe's 8191-character line.
pushd "%T%"
set "ARGS="
for %%f in (cpp\*.png) do set ARGS=!ARGS! "%%f" "ref\%%~nf.font"
dotnet "%O%" --png2font !ARGS! || exit /b 1
popd
set /a N=0, BAD=0
for %%f in ("%T%\cpp\*.png") do (
  "%E%" -png2font "%%f" 2>nul
  set /a N+=1
  fc /b "%T%\ref\%%~nf.font" "%T%\cpp\%%~nf.font" >nul || (echo DIFF %%~nf & set /a BAD+=1)
)
for %%f in ("%~dp0..\Data\fonts\*.font") do if exist "%T%\cpp\%%~nxf" (
  set /a N+=1
  fc /b "%%f" "%T%\cpp\%%~nxf" >nul || (echo DIFF committed %%~nxf & set /a BAD+=1)
)
echo png2font: !N! comparisons, !BAD! different
if !BAD! neq 0 exit /b 1
