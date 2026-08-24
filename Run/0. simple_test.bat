@echo off
setlocal

echo ============================================
echo   Quick Test - Server + Client (No Monitor)
echo ============================================
echo.

REM === 1. Kill running processes ===
echo [1/4] Killing running processes...
taskkill /F /IM MMOServer.exe >nul 2>nul
taskkill /F /IM GameClient.exe >nul 2>nul
echo   - Done
echo.

REM === 2. bin ?∞Ï∂úÎ¨??ïÏù∏ (?ÜÏúºÎ©?.build.bat Î®ºÏ?) ===
echo [2/4] Checking build output...
if not exist "%~dp0bin\MMOServer.exe" (
    echo [MISSING] bin\MMOServer.exe
    goto :NEED_BUILD
)
if not exist "%~dp0bin\GameClient.exe" (
    echo [MISSING] bin\GameClient.exe
    goto :NEED_BUILD
)
echo   - OK
echo.

REM === 4. Configure ===
echo [3/4] Configuring...
powershell -Command "(Get-Content -Encoding Default '%~dp0bin\MMOServerConfig.ini') -replace '^Mode=.*', 'Mode=GameServer' -replace '^MonitorEnabled=.*', 'MonitorEnabled=0' | Set-Content -Encoding Default '%~dp0bin\MMOServerConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] MMOServerConfig.ini update failed!
    goto :ERROR
)
echo   - MMOServerConfig.ini updated (Mode=GameServer, MonitorEnabled=0)
echo.

REM === 5. Run ===
echo [4/4] Starting...
start "" /D "%~dp0bin" MMOServer.exe
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

:NEED_BUILD
echo.
echo ============================================
echo   [STOP] bin\ ???§Ìñâ ?åÏùº???ÜÏäµ?àÎã§.
echo   Î®ºÏ? .build.bat ???§Ìñâ?òÏÑ∏?? (?ÑÏ≤¥ ÎπåÎìú: .build.bat)
echo ============================================
pause
exit /b 1

:ERROR
echo.
echo ============================================
echo   [FAILED] Error occurred. Check log above.
echo ============================================
pause
exit /b 1
