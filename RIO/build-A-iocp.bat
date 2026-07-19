@echo off
setlocal
rem RIO A/B arm switcher: arm A = IOCP baseline (USE_RIO_TRANSPORT=0) + rebuild Release x64
set "BC=%~dp0..\IOCP_Server\IOCP_Server\BuildConfig.h"
powershell -NoProfile -Command "$t=[IO.File]::ReadAllText('%BC%'); $t=$t -replace '#define USE_RIO_TRANSPORT \d', '#define USE_RIO_TRANSPORT 0'; [IO.File]::WriteAllText('%BC%', $t, (New-Object Text.UTF8Encoding($true)))"
set "MSB=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSB%" set "MSB=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSB%" echo [build] MSBuild not found
if not exist "%MSB%" exit /b 1
"%MSB%" "%~dp0..\IOCP_Server\IOCP_Server.sln" /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
if errorlevel 1 exit /b 1
echo [OK] arm=A IOCP (USE_RIO_TRANSPORT=0) -^> Run\bin\IOCP_Server.exe
exit /b 0
