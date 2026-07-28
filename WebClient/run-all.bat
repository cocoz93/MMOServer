@echo off
rem 전체 실행 - 서버 + 더미 + 릴레이 + 브라우저 클라 한 번에.
powershell -ExecutionPolicy Bypass -File "%~dp0_launch.ps1" %*
echo.
pause