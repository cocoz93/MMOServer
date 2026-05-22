@echo off
REM === 창이 바로 닫히지 않도록 cmd /k 로 재실행 ===
if not defined _RELAUNCH (
    set "_RELAUNCH=1"
    cmd /k "%~f0" %*
    exit /b
)
setlocal

echo ============================================
echo   MMO Stress Test - GameServer Mode
echo ============================================
echo.

REM === 1. Kill running processes ===
tasklist /FI "IMAGENAME eq IOCP_Server.exe" | findstr /I "IOCP_Server.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo [1/5] Killing running processes...
    taskkill /F /IM IOCP_Server.exe >nul 2>nul
    echo   - Server killed
    taskkill /F /IM MMOStressClient.exe >nul 2>nul
    echo   - MMOStressClient killed
    taskkill /F /IM prometheus.exe >nul 2>nul
    taskkill /F /IM windows_exporter.exe >nul 2>nul
    taskkill /F /IM grafana.exe >nul 2>nul
    echo   - Monitoring killed
    echo.
) else (
    echo [1/5] No running processes found.
    echo.
)

REM === 2. MSBuild path ===
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    goto :ERROR
)

REM === 3. Build (Release x64) ===
echo [2/5] Building...
echo   - Building Server...
"%MSBUILD%" "%~dp0..\IOCP_Server\IOCP_Server.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    goto :ERROR
)
echo   - Server build OK

echo   - Building MMOStressClient...
"%MSBUILD%" "%~dp0..\StressTest\3. MMO_stress\MMOStressClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] MMOStressClient build failed!
    goto :ERROR
)
echo   - MMOStressClient build OK
echo.

REM === 4. Configure ===
echo [3/5] Configuring...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini') -replace '^Mode=.*', 'Mode=GameServer' -replace '^MonitorEnabled=.*', 'MonitorEnabled=1' | Set-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] ServerConfig.ini update failed!
    goto :ERROR
)
echo   - ServerConfig.ini updated (Mode=GameServer, MonitorEnabled=1)
echo.

REM === 5. Start Monitoring ===
echo [4/5] Starting monitoring...
start "" "%~dp0..\Monitoring\windows_exporter.exe"
echo   - windows_exporter started (:9182)

start "" /D "%~dp0..\Monitoring\prometheus-3.4.1.windows-amd64" prometheus.exe --web.listen-address=":9091"
echo   - Prometheus started (:9091)

start "" /D "%~dp0..\Monitoring\grafana\bin" grafana-server.exe
echo   - Grafana started (:3000)

timeout /t 3 /nobreak >nul
start http://localhost:9091
start http://localhost:3000
echo.

REM === 6. Run ===
echo [5/5] Starting...
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

start "" /D "%~dp0bin" MMOStressClient.exe
echo   - MMOStressClient started

start "" /D "%~dp0bin" GameClient.exe
echo   - GameClient started (manual play)
echo.

echo ============================================
echo   Done! All services running.
echo   Prometheus UI : http://localhost:9091
echo   Grafana       : http://localhost:3000
echo   (Grafana login: admin / admin)
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
