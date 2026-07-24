@echo off
REM ============================================================
REM  RIO vs IOCP transport comparison (Phase 2 measurement)
REM    full  (~2h) :  rio-ab.bat
REM    smoke (~10m):  rio-ab.bat smoke
REM    +N7 diag    :  rio-ab.bat -IncludeN7
REM  out: Monitoring\metrics_out\window_metrics.csv (RIO_IOCP_r# / RIO_N2_r# / ...)
REM ============================================================
setlocal
set PS=powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0rio-ab.ps1"
if /I "%~1"=="smoke" ( %PS% -Smoke ) else ( %PS% %* )
endlocal
pause
