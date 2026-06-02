@echo off
REM ============================================
REM   MMO Stress - Client Only
REM   .build.bat 으로 먼저 빌드한 뒤, 이 배치로 클라이언트 기동
REM
REM   [서버 IP 설정]
REM   이 배치는 IP를 건드리지 않는다. 접속할 서버 IP는 ini에서 직접 설정:
REM     - bin\MMOStressConfig.ini  -> ServerIp=<서버IP>   (더미 스트레스 클라)
REM     - bin\ClientConfig.ini     -> [Server] IP=<서버IP> (수동 플레이 클라)
REM   ini에 값이 없으면 소스코드 기본값(127.0.0.1)으로 폴백된다.
REM ============================================
setlocal

echo ============================================
echo   MMO Stress - Client Only
echo ============================================
echo.

REM === 1. 기존 프로세스 종료 ===
echo [1/3] Killing running client...
taskkill /F /IM MMOStressClient.exe >nul 2>nul
taskkill /F /IM GameClient.exe >nul 2>nul
echo   - Done
echo.

REM === 2. bin 산출물 확인 (없으면 .build.bat 먼저) ===
echo [2/3] Checking build output...
if not exist "%~dp0bin\MMOStressClient.exe" (
    echo [MISSING] bin\MMOStressClient.exe
    goto :NEED_BUILD
)
if not exist "%~dp0bin\GameClient.exe" (
    echo [MISSING] bin\GameClient.exe
    goto :NEED_BUILD
)
echo   - OK
echo.

REM === 3. 실행 (서버IP는 ini에서 읽음, 배치는 건드리지 않음) ===
echo [3/3] Starting...
start "" /D "%~dp0bin" MMOStressClient.exe
echo   - MMOStressClient started

start "" /D "%~dp0bin" GameClient.exe
echo   - GameClient started (manual play)
echo.

echo ============================================
echo   Clients running. (Target server IP: see ini)
echo   - MMOStressClient (stress)
echo   - GameClient (manual play)
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
