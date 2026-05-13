@echo off
setlocal

echo ============================================
echo   Build and Run - Server / Client
echo ============================================
echo.

REM === 1. Kill running processes ===
echo [1/4] Killing running processes...
taskkill /F /IM IOCP_MMOServer.exe 2>nul && echo   - Server killed || echo   - Server not running
taskkill /F /IM IOCP_MMOClient.exe 2>nul && echo   - Client killed || echo   - Client not running
taskkill /F /IM prometheus.exe 2>nul && echo   - Prometheus killed || echo   - Prometheus not running
taskkill /F /IM windows_exporter.exe 2>nul && echo   - windows_exporter killed || echo   - windows_exporter not running
taskkill /F /IM grafana.exe 2>nul && echo   - Grafana killed || echo   - Grafana not running
echo.

REM === 2. MSBuild path ===
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    pause
    exit /b 1
)

REM === 3. Build ===
echo [2/4] Building...
echo   - Building Server...
"%MSBUILD%" "%~dp0IOCP_MMOServer\IOCP_MMOServer.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    pause
    exit /b 1
)
echo   - Server build OK
echo.

echo   - Building Client...
"%MSBUILD%" "%~dp0IOCP_MMOClient\IOCP_MMOClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Client build failed!
    pause
    exit /b 1
)
echo   - Client build OK
echo.

REM === 4. Run Monitoring ===
echo [3/4] Starting monitoring...
start "" "%~dp0Monitoring\windows_exporter.exe"
echo   - windows_exporter started (:9182)

start "" /D "%~dp0Monitoring\prometheus-3.4.1.windows-amd64" prometheus.exe --web.listen-address=":9091"
echo   - Prometheus started (:9091)

start "" /D "%~dp0Monitoring\grafana\bin" grafana-server.exe
echo   - Grafana started (:3000)
echo.

REM === 5. Run Server / Client ===
echo [4/4] Starting server and client...
start "" "%~dp0IOCP_MMOServer\x64\Release\IOCP_MMOServer.exe"
echo   - Server started (:6000, metrics :9090)

timeout /t 2 /nobreak >nul

start "" "%~dp0IOCP_MMOClient\x64\Release\IOCP_MMOClient.exe"
echo   - Client started
echo.

echo ============================================
echo   Done! All services are running.
echo   Prometheus UI: http://localhost:9091
echo   Grafana:       http://localhost:3000
echo   (Grafana login: admin / admin)
echo ============================================
pause
