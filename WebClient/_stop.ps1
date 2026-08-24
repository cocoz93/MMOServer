<#  데모 종료: 릴레이/더미(/선택적 서버) 정리  #>
param([switch]$StopServer)
$here = $PSScriptRoot
# 릴레이(node) — 기록된 PID만 종료
$pidFile = Join-Path $here '.relay.pid'
if(Test-Path $pidFile){
  $pid0 = Get-Content $pidFile
  # PID 재사용 대비 — node 인지 확인하고 죽인다
  $proc = Get-Process -Id $pid0 -ErrorAction SilentlyContinue
  if($proc -and $proc.ProcessName -eq 'node'){ Stop-Process -Id $pid0 -Force; Write-Host "릴레이 종료 (pid $pid0)" }
  elseif($proc){ Write-Host "pid $pid0 은 node 가 아님($($proc.ProcessName)) — 건드리지 않음" }
  else{ Write-Host "릴레이 프로세스 없음" }
  Remove-Item $pidFile -ErrorAction SilentlyContinue
}
# 더미
Get-Process MMOStressClient -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "더미 종료"
# 서버는 기본 유지(다른 작업 중일 수 있음). -StopServer 주면 종료
if($StopServer){ Get-Process MMOServer -ErrorAction SilentlyContinue | Stop-Process -Force; Write-Host "서버 종료" }
else { Write-Host "서버는 유지 (종료하려면: stop.bat -StopServer)" }
