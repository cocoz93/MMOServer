@echo off
setlocal EnableDelayedExpansion

REM === Client count (default: 1) ===
set CLIENT_COUNT=1
if not "%~1"=="" set CLIENT_COUNT=%~1

echo ============================================
echo   Echo Test + Monitor (x%CLIENT_COUNT%)
echo ============================================
echo.

REM === 1. Kill running processes ===
echo [1/5] Killing running processes...
taskkill /F /IM IOCP_Server.exe >nul 2>nul
taskkill /F /IM EchoStressClient.exe >nul 2>nul
taskkill /F /IM prometheus.exe >nul 2>nul
taskkill /F /IM windows_exporter.exe >nul 2>nul
taskkill /F /IM grafana-server.exe >nul 2>nul
echo   - Done
echo.

REM === 2. bin 산출물 확인 (없으면 .build.bat 먼저) ===
echo [2/5] Checking build output...
if not exist "%~dp0bin\IOCP_Server.exe" (
    echo [MISSING] bin\IOCP_Server.exe
    goto :NEED_BUILD
)
if not exist "%~dp0bin\EchoStressClient.exe" (
    echo [MISSING] bin\EchoStressClient.exe
    goto :NEED_BUILD
)
echo   - OK
echo.

REM === 4. Configure ===
echo [3/5] Configuring...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini') -replace '^Mode=.*', 'Mode=NetWorkLib_EchoTest' -replace '^MonitorEnabled=.*', 'MonitorEnabled=1' | Set-Content -Encoding UTF8 '%~dp0bin\ServerConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] ServerConfig.ini update failed!
    goto :ERROR
)
echo   - ServerConfig.ini updated (Mode=NetWorkLib_EchoTest, MonitorEnabled=1)

set "PROM_YML=%~dp0..\Monitoring\prometheus-3.4.1.windows-amd64\prometheus.yml"
set STRESS_TARGETS="localhost:9092"
for /L %%i in (2,1,%CLIENT_COUNT%) do (
    set /a PORT=9091+%%i
    set STRESS_TARGETS=!STRESS_TARGETS!, "localhost:!PORT!"
)
(
echo global:
echo   scrape_interval: 5s
echo   evaluation_interval: 15s
echo.
echo scrape_configs:
echo   - job_name: prometheus
echo     static_configs:
echo       - targets: ["localhost:9091"]
echo.
echo   - job_name: mmo_server
echo     static_configs:
echo       - targets: ["localhost:9090"]
echo.
echo   - job_name: stress_client
echo     static_configs:
echo       - targets: [%STRESS_TARGETS%]
echo.
echo   - job_name: windows
echo     static_configs:
echo       - targets: ["localhost:9182"]
) > "%PROM_YML%"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] prometheus.yml update failed!
    goto :ERROR
)
echo   - prometheus.yml updated (stress_client: %STRESS_TARGETS%)
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

start "" /D "%~dp0bin" EchoStressClient.exe
echo   - EchoStressClient #1 started (MonitorPort=9092)
for /L %%i in (2,1,%CLIENT_COUNT%) do (
    start "" /D "%~dp0bin" EchoStressClient.exe StressConfig%%i.ini
    echo   - EchoStressClient #%%i started (StressConfig%%i.ini)
)
echo.

echo ============================================
echo   Done! All services running. (Clients: %CLIENT_COUNT%)
echo   Prometheus UI : http://localhost:9091
echo   Grafana       : http://localhost:3000
echo   (Grafana login: admin / admin)
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
