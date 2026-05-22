@echo off
REM === 창이 바로 닫히지 않도록 cmd /k 로 재실행 ===
if not defined _RELAUNCH (
    set "_RELAUNCH=1"
    cmd /k "%~f0" %*
    exit /b
)
setlocal

echo ============================================
echo   Quick Test - Server + Client (No Monitor)
echo ============================================
echo.

REM === 1. Kill running processes ===
echo [1/4] Killing running processes...
taskkill /F /IM IOCP_Server.exe >nul 2>nul
taskkill /F /IM GameClient.exe >nul 2>nul
echo   - Done
echo.

REM === 2. MSBuild path ===
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    goto :ERROR
)

REM === 3. Build (Release x64) ===
echo [2/4] Building...
echo   - Building Server...
"%MSBUILD%" "%~dp0..\IOCP_Server\IOCP_Server.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    goto :ERROR
)
echo   - Server build OK

echo   - Building Client...
"%MSBUILD%" "%~dp0..\GameClient\GameClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Client build failed!
    goto :ERROR
)
echo   - Client build OK
echo.

REM === 4. Configure ===
echo [3/4] Configuring...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini') -replace '^Mode=.*', 'Mode=GameServer' -replace '^MonitorEnabled=.*', 'MonitorEnabled=0' | Set-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] ServerConfig.ini update failed!
    goto :ERROR
)
echo   - ServerConfig.ini updated (Mode=GameServer, MonitorEnabled=0)
echo.

REM === 5. Run ===
echo [4/4] Starting...
start "" /D "%~dp0bin" IOCP_Server.exe
echo   - Server started

echo   - Waiting for server to listen on port 6000...
set WAIT_COUNT=0
:WAIT_SERVER
netstat -an | findstr "LISTENING" | findstr ":6000" >nul
if %ERRORLEVEL% EQU 0 goto SERVER_READY
set /a WAIT_COUNT+=1
if %WAIT_COUNT% GEQ 30 (
    echo [ERROR] Server did not start within 30 seconds!
    goto :ERROR
)
timeout /t 1 /nobreak >nul
goto WAIT_SERVER
:SERVER_READY
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
exit /b 0

:ERROR
echo.
echo ============================================
echo   [FAILED] Error occurred. Check log above.
echo ============================================
pause
exit /b 1
