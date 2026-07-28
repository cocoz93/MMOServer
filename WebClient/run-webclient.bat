@echo off
rem 웹 클라이언트만 실행 - 서버/더미는 평소대로(Run\3-1 등) 켠 뒤 실행.
rem   릴레이(있으면 재사용) 기동 + 브라우저로 live.html 열기
powershell -ExecutionPolicy Bypass -File "%~dp0_launch.ps1" -SkipServer -SkipDummies
echo.
pause