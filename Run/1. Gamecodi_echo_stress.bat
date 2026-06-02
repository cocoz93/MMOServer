@echo off
setlocal

echo ============================================
echo   Echo Stress Test - GameCodiEchoTest Mode
echo ============================================
echo.

REM === 1. Kill running processes ===
echo [1/4] Killing running processes...
taskkill /F /IM IOCP_Server.exe >nul 2>nul
taskkill /F /IM LanServer_StressTest_20191125.exe >nul 2>nul
echo   - Done
echo.

REM === 2. bin 산출물 확인 (없으면 .build.bat 먼저) ===
echo [2/4] Checking build output...
if not exist "%~dp0bin\IOCP_Server.exe" (
    echo [MISSING] bin\IOCP_Server.exe
    goto :NEED_BUILD
)
echo   - OK
echo.

REM === 4. Configure ===
echo [3/4] Configuring...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0bin\IOCP_ServerConfig.ini') -replace '^Mode=.*', 'Mode=GameCodiEchoTest' -replace '^MonitorEnabled=.*', 'MonitorEnabled=0' | Set-Content -Encoding UTF8 '%~dp0bin\IOCP_ServerConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] IOCP_ServerConfig.ini update failed!
    goto :ERROR
)
echo   - IOCP_ServerConfig.ini updated (Mode=GameCodiEchoTest, MonitorEnabled=0)
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

start "" /D "%~dp0..\StressTest\1. GameCodiStressTest" LanServer_StressTest_20191125.exe
echo   - StressTest started
echo.

echo ============================================
echo   Done! Echo stress test running.
echo ============================================
pause
exit /b 0

:NEED_BUILD
echo.
echo ============================================
echo   [STOP] bin\ 에 실행 파일이 없습니다.
echo   먼저 .build.bat 을 실행하세요. (전체 빌드: .build.bat)
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
