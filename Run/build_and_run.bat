@echo off
setlocal

echo ============================================
echo   Build and Run - Server / Client
echo ============================================
echo.

REM === 1. Kill running processes ===
echo [1/4] Killing running processes...
taskkill /F /IM IOCP_Server.exe 2>nul && echo   - Server killed || echo   - Server not running
taskkill /F /IM GameClient.exe 2>nul && echo   - Client killed || echo   - Client not running
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
"%MSBUILD%" "%~dp0..\IOCP_Server\IOCP_Server.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Server build failed!
    pause
    exit /b 1
)
echo   - Server build OK
echo.

echo   - Building Client...
"%MSBUILD%" "%~dp0..\GameClient\GameClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Client build failed!
    pause
    exit /b 1
)
echo   - Client build OK
echo.

set "BIN_DIR=%~dp0bin"

REM === 4. Run Monitoring ===
echo [3/4] Starting monitoring...
start "" "%~dp0..\Monitoring\windows_exporter.exe"
echo   - windows_exporter started (:9182)

start "" /D "%~dp0..\Monitoring\prometheus-3.4.1.windows-amd64" prometheus.exe --web.listen-address=":9091"
echo   - Prometheus started (:9091)

start "" /D "%~dp0..\Monitoring\grafana\bin" grafana-server.exe
echo   - Grafana started (:3000)

REM Wait briefly for services to initialize, then open in browser
timeout /t 3 /nobreak >nul
start http://localhost:9091
echo   - Prometheus UI opened in browser
start http://localhost:3000
echo   - Grafana UI opened in browser
echo.

REM === 5. Set GameServer mode ===
echo [4/4] Setting GameServer mode...
powershell -Command "(Get-Content '%BIN_DIR%\ServerConfig.ini') -replace '^Mode=.*', 'Mode=GameServer' | Set-Content '%BIN_DIR%\ServerConfig.ini'"
echo   - ServerConfig.ini updated (Mode=GameServer)
echo.

REM === 6. Run Server / Client ===
echo Starting server and client...
start "" /D "%BIN_DIR%" IOCP_Server.exe
echo   - Server started (Run directory)

echo   - Waiting for server to listen on port 6000...
:WAIT_SERVER
netstat -an | findstr "LISTENING" | findstr ":6000" >nul
if %ERRORLEVEL% NEQ 0 (
    timeout /t 1 /nobreak >nul
    goto WAIT_SERVER
)
echo   - Server is ready

start "" /D "%BIN_DIR%" GameClient.exe
echo   - Client started (Run directory)
echo.

echo ============================================
echo   Done! All services are running.
echo   Run directory : %BIN_DIR%
echo   Prometheus UI : http://localhost:9091
echo   Grafana       : http://localhost:3000
echo   (Grafana login: admin / admin)
echo ============================================
pause
