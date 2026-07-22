@echo off
REM ============================================================
REM  GameCore isolation CEILING SWEEP - off vs on, ClientCount [5000..5600]
REM    full  (~1.4h):  "8. Ceiling_Sweep.bat"
REM    smoke (~8min):  "8. Ceiling_Sweep.bat" smoke
REM  out: Monitoring\metrics_out\window_metrics.csv (CEIL_cc*_A_off / CEIL_cc*_B_on)
REM ============================================================
setlocal
set PS=powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0affinity-ceiling-sweep.ps1"
if /I "%~1"=="smoke" (
  %PS% -Smoke
) else (
  %PS%
)
endlocal
pause
