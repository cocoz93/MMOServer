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

REM === 2. MSBuild path ===
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    pause
    exit /b 1
)

REM === 3. Build (Release x64) ===
echo [2/4] Building Server...
"%MSBUILD%" "%~dp0..\IOCP_Server\IOCP_Server.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    pause
    exit /b 1
)
echo   - Server build OK
echo.

REM === 4. Configure ===
echo [3/4] Configuring...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini') -replace '^Mode=.*', 'Mode=GameCodiEchoTest' -replace '^MonitorEnabled=.*', 'MonitorEnabled=0' | Set-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini'"
echo   - ServerConfig.ini updated (Mode=GameCodiEchoTest, MonitorEnabled=0)
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
    pause
    exit /b 1
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
