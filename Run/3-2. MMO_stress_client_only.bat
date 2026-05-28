@echo off
REM ============================================
REM   MMO Stress - Client Only
REM   클론 후 이 배치만 실행하면 빌드 + 클라이언트 기동
REM   사용법: "3-2. MMO_stress_client_only.bat" [서버IP]
REM   기본값: 127.0.0.1
REM ============================================
if not defined _RELAUNCH (
    set "_RELAUNCH=1"
    cmd /k "%~f0" %*
    exit /b
)
setlocal

REM === 서버 IP (인자 없으면 127.0.0.1) ===
set SERVER_IP=%~1
if "%SERVER_IP%"=="" set SERVER_IP=127.0.0.1

echo ============================================
echo   MMO Stress - Client Only
echo   Target: %SERVER_IP%:6000
echo ============================================
echo.

REM === 1. 기존 프로세스 종료 ===
echo [1/3] Killing running client...
taskkill /F /IM MMOStressClient.exe >nul 2>nul
echo   - Done
echo.

REM === 2. 빌드 ===
echo [2/3] Building MMOStressClient...
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found!
    goto :ERROR
)
"%MSBUILD%" "%~dp0..\StressTest\3. MMO_stress\MMOStressClient.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] MMOStressClient build failed!
    goto :ERROR
)
echo   - Build OK
echo.

REM === 3. ini 서버IP 변경 + 실행 ===
echo [3/3] Configuring and starting...
powershell -Command "(Get-Content -Encoding UTF8 '%~dp0bin\MMOStressConfig.ini') -replace '^ServerIp=.*', 'ServerIp=%SERVER_IP%' | Set-Content -Encoding UTF8 '%~dp0bin\MMOStressConfig.ini'"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] MMOStressConfig.ini update failed!
    goto :ERROR
)
echo   - MMOStressConfig.ini updated (ServerIp=%SERVER_IP%)

start "" /D "%~dp0bin" MMOStressClient.exe
echo   - MMOStressClient started
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
