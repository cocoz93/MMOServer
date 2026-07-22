@echo off
REM ============================================================
REM  GameCore 코어 격리 A/B (무인) - off(baseline) vs on(게임루프 전용 코어)
REM    본실험 (off/on x3, 약 65분):  "7. Affinity_AB.bat"
REM    드라이런 (약 8분):            "7. Affinity_AB.bat" smoke
REM  결과: Monitoring\metrics_out\window_metrics.csv (AFF_A_off_* / AFF_B_on_*)
REM ============================================================
setlocal
set PS=powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0affinity-ab.ps1"
if /I "%~1"=="smoke" (
  %PS% -Reps 1 -LoadMin 1 -WindowMin 1 -LabelPrefix SMOKE
) else (
  %PS%
)
endlocal
pause
