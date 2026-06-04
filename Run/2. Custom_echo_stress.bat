@echo off
setlocal

echo ============================================
echo   Echo Test + Monitor
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

REM === 2. bin ?∞Ï∂úÎ¨??ïÏù∏ (?ÜÏúºÎ©?.build.bat Î®ºÏ?) ===
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
powershell -Command "(Get-Content -Encoding Default '%~dp0bin\IOCP_ServerConfig.ini') -replace '^Mode=.*', 'Mode=NetWorkLib_EchoTest' -replace '^MonitorEnabled=.*', 'MonitorEnabled=1' | Set-Content -Encoding Default '%~dp0bin\IOCP_ServerConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] IOCP_ServerConfig.ini update failed!
    goto :ERROR
)
echo   - IOCP_ServerConfig.ini updated (Mode=NetWorkLib_EchoTest, MonitorEnabled=1)

set "PROM_YML=%~dp0..\Monitoring\prometheus-3.4.1.windows-amd64\prometheus.yml"
set STRESS_TARGETS="localhost:9092"
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
echo   - EchoStressClient started (MonitorPort=9092)
echo.

echo ============================================
echo   Done! All services running.
echo   Prometheus UI : http://localhost:9091
echo   Grafana       : http://localhost:3000
echo   (Grafana login: admin / admin)
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
