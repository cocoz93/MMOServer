@echo off
REM ============================================
REM   Echo Stress - Client Only
REM   클론 후 이 배치만 실행하면 빌드 + 클라이언트 기동
REM   사용법: "2-2. Echo_stress_client_only.bat" [서버IP]
REM   예시 : "2-2. Echo_stress_client_only.bat" 192.168.0.10
REM ============================================
if not defined _RELAUNCH (
    set "_RELAUNCH=1"
    cmd /k "%~f0" %*
    exit /b
)
setlocal EnableDelayedExpansion

REM === 서버 IP (인자 없으면 127.0.0.1) ===
set SERVER_IP=%~1
if "%SERVER_IP%"=="" set SERVER_IP=127.0.0.1

echo ============================================
echo   Echo Stress - Client Only
echo   Target: %SERVER_IP%:6000
echo ============================================
echo.

REM === 1. 기존 프로세스 종료 ===
echo [1/3] Killing running client...
taskkill /F /IM EchoStressClient.exe >nul 2>nul
echo   - Done
echo.

REM === 2. 빌드 ===
echo [2/3] Building EchoStressClient...
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    goto :ERROR
)
"%MSBUILD%" "%~dp0..\StressTest\2. Custom_echo_stress\EchoStressClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] EchoStressClient build failed!
    goto :ERROR
)
echo   - Build OK
echo.

REM === 3. ini 서버IP 변경 + 실행 ===
echo [3/3] Configuring and starting...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0bin\EchoStressConfig.ini') -replace '^ServerIp=.*', 'ServerIp=%SERVER_IP%' | Set-Content -Encoding UTF8 '%~dp0bin\EchoStressConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] EchoStressConfig.ini update failed!
    goto :ERROR
)
echo   - EchoStressConfig.ini updated (ServerIp=%SERVER_IP%)

start "" /D "%~dp0bin" EchoStressClient.exe
echo   - EchoStressClient started
echo.

echo ============================================
echo   Client running. Target: %SERVER_IP%:6000
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
