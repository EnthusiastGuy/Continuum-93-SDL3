@echo off
rem Replays the oracle vectors for the given mnemonics (all when none): conf ADD SUB
"%~dp0..\build\release\conformance.exe" "%~dp0..\build\vectors.txt" %*
