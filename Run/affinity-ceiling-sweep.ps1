<#
.SYNOPSIS
  GameCore 격리 접속 천장 스윕 — off/on 각 팔의 접속 천장을 비교한다.
  천장 = tick_p99<=41ms · 송신버퍼 포화=0 · session>=CC 를 마지막으로 지킨 최고 ClientCount.

.DESCRIPTION
  검증된 affinity-ab.ps1(off/on A/B 러너)을 ClientCount마다 1회씩 호출한다(리빌드 없음).
  라벨: CEIL_cc<N>_A_off_r<rep> / CEIL_cc<N>_B_on_r<rep> — 공유 CSV(window_metrics.csv)에 누적.
  스윕 끝에 팔별 [CC -> tick_p99·tick_avg·sendworker_cpu_max·buffer_full·session] 표와 천장 판정을 출력.

  가설: 격리 on이 게임루프를 가볍게 해 천장을 올린다. 반례: I/O가 물리6->5코어로 눌려 꼬리지연이 먼저 터지면 on이 더 낮은 천장.
  전제: affinity-ab.ps1과 동일 — USE_RIO_TRANSPORT=0 빌드, 부하클라 물리6-9 핀, MySQL 기동(이미 충족: 직전 full run 완주).
  주의: 부하생성기 물리4코어라 ~5500 부근이 생성 한계. session<CC 이면 클라 병목이라 그 점은 '서버 천장'으로 못 쓴다(health에 표시).

.EXAMPLE
  .\affinity-ceiling-sweep.ps1
  # 본 스윕: [5000,5200,5400,5600] x off/on x 1rep, LoadMin=6 WindowMin=3 (약 1.4시간)

  .\affinity-ceiling-sweep.ps1 -Smoke
  # 배관 점검: cc5000 1점 x off/on x 1분 (약 8분) — 래퍼+요약 파서 검증용
#>
param(
    [int]    $Reps      = 1,
    [int]    $LoadMin   = 6,
    [int]    $WindowMin = 3,
    [switch] $Smoke
)

$ErrorActionPreference = "Stop"
$RunDir   = $PSScriptRoot
$AbScript = Join-Path $RunDir "affinity-ab.ps1"
$Root     = Split-Path $RunDir -Parent
$Mon      = Join-Path $Root "Monitoring"
$OutDir   = Join-Path $Mon "metrics_out"
$Csv      = Join-Path $OutDir "window_metrics.csv"

# 스윕 지점은 본문에 고정한다. 콤마 int배열을 -File 인자로 넘기면 오파싱되므로 파라미터로 받지 않는다.
if ($Smoke) {
    $ClientCounts = @(5000)
    $Reps = 1; $LoadMin = 1; $WindowMin = 1
}
else {
    $ClientCounts = @(5000, 5200, 5400, 5600)
}

# CSV 문자열 -> double(InvariantCulture). 빈값/파싱불가면 $null.
function ToNum([string]$s) {
    $d = 0.0
    if ([double]::TryParse($s, [Globalization.NumberStyles]::Float,
                           [Globalization.CultureInfo]::InvariantCulture, [ref]$d)) { $d } else { $null }
}
function GetMetric($rows, $lbl, $m) {
    ($rows | Where-Object { $_.RunLabel -eq $lbl -and $_.Metric -eq $m } | Select-Object -Last 1).Value
}
function AvgOf($arr) {
    $v = $arr | Where-Object { $_ -ne $null }
    if ($v.Count) { ($v | Measure-Object -Average).Average } else { $null }
}

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
Start-Transcript -Path (Join-Path $OutDir ("ceiling_sweep_{0}.log" -f (Get-Date -Format "yyyyMMdd_HHmmss"))) | Out-Null

$ccList = ($ClientCounts -join ", ")
Write-Host "=== Ceiling sweep: ClientCount in [$ccList] x off/on x $Reps rep  (LoadMin=$LoadMin WindowMin=$WindowMin) ===" -ForegroundColor Cyan

try {
    # -- 1) 스윕: ClientCount마다 검증된 off/on 러너를 호출 --
    foreach ($cc in $ClientCounts) {
        $prefix = "CEIL_cc$cc"
        Write-Host ""
        Write-Host ">>> ClientCount=$cc  (off/on x $Reps)  -> prefix $prefix" -ForegroundColor Magenta
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $AbScript `
            -ClientCount $cc -Reps $Reps -LoadMin $LoadMin -WindowMin $WindowMin -LabelPrefix $prefix
        if ($LASTEXITCODE -ne 0) { Write-Warning "affinity-ab.ps1 exited $LASTEXITCODE for cc=$cc (continuing)" }
    }

    # -- 2) 천장 요약: 팔별 CC 곡선 + 첫 붕괴 직전 CC --
    Write-Host ""
    Write-Host "=== CEILING SUMMARY ===" -ForegroundColor Green
    if (-not (Test-Path $Csv)) { Write-Warning "no CSV: $Csv"; return }
    $all = Import-Csv $Csv

    $armMap = @{ "off" = "A_off"; "on" = "B_on" }
    $ceil   = @{}
    foreach ($arm in @("off", "on")) {
        $tag = $armMap[$arm]
        Write-Host ""
        Write-Host "--- Arm: $arm ---" -ForegroundColor Yellow
        $brokenYet = $false
        $table = foreach ($cc in $ClientCounts) {
            $tp99 = @(); $tavg = @(); $scmax = @(); $bfull = @(); $sess = @()
            for ($r = 1; $r -le $Reps; $r++) {
                $lbl = "CEIL_cc${cc}_${tag}_r$r"
                $tp99  += (ToNum (GetMetric $all $lbl "tick_p99_ms"))
                $tavg  += (ToNum (GetMetric $all $lbl "tick_avg_ms"))
                $scmax += (ToNum (GetMetric $all $lbl "sendworker_cpu_max"))
                $bfull += (ToNum (GetMetric $all $lbl "dummy_send_buffer_full_rate"))
                $sess  += (ToNum (GetMetric $all $lbl "session_count"))
            }
            $vp99 = AvgOf $tp99; $vavg = AvgOf $tavg; $vscm = AvgOf $scmax; $vbf = AvgOf $bfull; $vses = AvgOf $sess

            $clientLimited = ($vses -ne $null -and $vses -lt ($cc * 0.97))
            $broken = ($vp99 -eq $null) -or ($vp99 -gt 41) -or ($vbf -gt 0) -or $clientLimited
            $note = if ($clientLimited) { "session<CC (load-gen limited?)" }
                    elseif ($broken)    { "BROKEN" }
                    else                { "ok" }
            if (-not $brokenYet -and -not $broken) { $ceil[$arm] = $cc }
            if ($broken) { $brokenYet = $true }

            [pscustomobject]@{
                CC          = $cc
                tick_p99    = if ($vp99 -ne $null) { "{0:N1}" -f $vp99 } else { "-" }
                tick_avg    = if ($vavg -ne $null) { "{0:N1}" -f $vavg } else { "-" }
                sw_cpu_max  = if ($vscm -ne $null) { "{0:N2}" -f $vscm } else { "-" }
                buffer_full = if ($vbf  -ne $null) { $vbf } else { "-" }
                session     = if ($vses -ne $null) { "{0:N0}" -f $vses } else { "-" }
                health      = $note
            }
        }
        $table | Format-Table -AutoSize
    }

    Write-Host ""
    Write-Host "=== VERDICT ===" -ForegroundColor Green
    $co = if ($ceil.ContainsKey("off")) { $ceil["off"] } else { "<$($ClientCounts[0])" }
    $cn = if ($ceil.ContainsKey("on"))  { $ceil["on"] }  else { "<$($ClientCounts[0])" }
    Write-Host ("  off ceiling ~= {0}   /   on ceiling ~= {1}" -f $co, $cn) -ForegroundColor White
    Write-Host "  (ceiling = highest CC keeping tick_p99<=41 . buffer_full=0 . session>=CC)"
    Write-Host "  (rows marked session<CC are load-gen limited - not valid as server ceiling)" -ForegroundColor DarkGray
    Write-Host "raw: $Csv  (CEIL_* labels)" -ForegroundColor DarkGray
}
finally {
    Stop-Transcript | Out-Null
}
