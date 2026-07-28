@echo off
rem 종료 - 릴레이/더미 정리(서버 유지). 서버까지 끄려면: stop.bat -StopServer
powershell -ExecutionPolicy Bypass -File "%~dp0_stop.ps1" %*
echo.
pause