@echo off
REM ============================================
REM   MMO Stress - Client Only
REM   .build.bat 으로 먼저 빌드한 뒤, 이 배치로 클라이언트 기동
REM   사용법: "3-2. MMO_stress_client_only.bat" [서버IP]
REM   기본값: 127.0.0.1
REM ============================================
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

REM === 2. bin 산출물 확인 (없으면 .build.bat 먼저) ===
echo [2/3] Checking build output...
if not exist "%~dp0bin\MMOStressClient.exe" (
    echo [MISSING] bin\MMOStressClient.exe
    goto :NEED_BUILD
)
echo   - OK
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
