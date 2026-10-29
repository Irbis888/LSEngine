@echo off
setlocal
set "engine=%~dp0..\..\x64\Release\Try2.exe"
if not exist "%engine%" set "engine=%~dp0..\..\x64\Debug\Try2.exe"
if not exist "%engine%" (
 echo Build Try2.sln x64 first.
 pause
 exit /b 1
)
"%engine%" --game "%~dp0game.json"
