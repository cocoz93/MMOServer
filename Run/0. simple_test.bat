@echo off
setlocal

echo ============================================
echo   Quick Test - Server + Client (No Monitor)
echo ============================================
echo.

REM === 1. Kill running processes ===
tasklist /FI "IMAGENAME eq IOCP_Server.exe" | findstr /I "IOCP_Server.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo [1/4] Killing running processes...
    taskkill /F /IM IOCP_Server.exe >nul 2>nul
    echo   - Server killed
    taskkill /F /IM GameClient.exe >nul 2>nul
    echo   - Client killed
    echo.
) else (
    echo [1/4] No running processes found.
    echo.
)

REM === 2. MSBuild path ===
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    pause
    exit /b 1
)

REM === 3. Build (Release x64) ===
echo [2/4] Building...
echo   - Building Server...
"%MSBUILD%" "%~dp0..\MMOServer\IOCP_Server.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    pause
    exit /b 1
)
echo   - Server build OK

echo   - Building Client...
"%MSBUILD%" "%~dp0..\GameClient\GameClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Client build failed!
    pause
    exit /b 1
)
echo   - Client build OK
echo.

REM === 4. Configure ===
echo [3/4] Configuring...
powershell -Command "(Get-Content '%~dp0bin\ServerConfig.ini') -replace '^Mode=.*', 'Mode=GameServer' -replace '^MonitorEnabled=.*', 'MonitorEnabled=0' | Set-Content '%~dp0bin\ServerConfig.ini'"
echo   - ServerConfig.ini updated (Mode=GameServer, MonitorEnabled=0)
echo.

REM === 5. Run ===
echo [4/4] Starting...
start "" /D "%~dp0bin" IOCP_Server.exe
echo   - Server started

echo   - Waiting for server to listen on port 6000...
:WAIT_SERVER
netstat -an | findstr "LISTENING" | findstr ":6000" >nul
if %ERRORLEVEL% NEQ 0 (
    timeout /t 1 /nobreak >nul
    goto WAIT_SERVER
)
echo   - Server is ready

start "" /D "%~dp0bin" GameClient.exe
echo   - Client 1 started
start "" /D "%~dp0bin" GameClient.exe
echo   - Client 2 started
echo.

echo ============================================
echo   Done! Server + Client running.
echo ============================================
pause
