@echo off
setlocal

echo ============================================
echo   Echo Test + Monitor - GameServer Mode
echo ============================================
echo.

REM === 1. Kill running processes ===
tasklist /FI "IMAGENAME eq IOCP_MMOServer.exe" | findstr /I "IOCP_MMOServer.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo [1/5] Killing running processes...
    taskkill /F /IM IOCP_MMOServer.exe >nul 2>nul
    echo   - Server killed
    taskkill /F /IM EchoStressClient.exe >nul 2>nul
    echo   - EchoStressClient killed
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
    pause
    exit /b 1
)

REM === 3. Build (Release x64) ===
echo [2/5] Building...
echo   - Building Server...
"%MSBUILD%" "%~dp0..\IOCP_MMOServer\IOCP_MMOServer.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    pause
    exit /b 1
)
echo   - Server build OK

echo   - Building EchoStressClient...
"%MSBUILD%" "%~dp0..\StressTest\EchoStressClient\EchoStressClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] EchoStressClient build failed!
    pause
    exit /b 1
)
echo   - EchoStressClient build OK
echo.

REM === 4. Configure ===
echo [3/5] Configuring...
powershell -Command "(Get-Content '%~dp0bin\ServerConfig.ini') -replace '^Mode=.*', 'Mode=GameServer' -replace '^MonitorEnabled=.*', 'MonitorEnabled=1' | Set-Content '%~dp0bin\ServerConfig.ini'"
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
start "" /D "%~dp0bin" IOCP_MMOServer.exe
echo   - Server started

echo   - Waiting for server to listen on port 6000...
:WAIT_SERVER
netstat -an | findstr "LISTENING" | findstr ":6000" >nul
if %ERRORLEVEL% NEQ 0 (
    timeout /t 1 /nobreak >nul
    goto WAIT_SERVER
)
echo   - Server is ready

start "" /D "%~dp0bin" EchoStressClient.exe
echo   - EchoStressClient started
echo.

echo ============================================
echo   Done! All services running.
echo   Prometheus UI : http://localhost:9091
echo   Grafana       : http://localhost:3000
echo   (Grafana login: admin / admin)
echo ============================================
pause
