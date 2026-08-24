<#
  gqcs-ab.ps1  -  Unattended GQCS vs GQCSEx completion-harvest A/B (IOCP arm only).

  QUESTION: how much of the IOCP baseline's network CPU is the "one syscall per completion"
  handicap of GetQueuedCompletionStatus, and from what load does batching start to pay?

  ARMS are a RUNTIME INI key (CompletionBatch), NOT a rebuild:
    CompletionBatch=0  -> GQCS   (one completion per syscall)  = baseline
    CompletionBatch=N  -> GQCSEx (up to N per syscall)
  Same binary for both arms -> no build difference, no incremental-build mislabel risk.
  The only defence left is the arm assert (mmo_completion_batch) -- it is enforced below.

  SWEEP AXIS IS LOAD, not batch cap. Batching only happens when completions are already
  queued, so the gain is a function of load. Measured early (echo probe, 1000 clients):
  cap 8 -> 3.81 completions/call, cap 64 -> 4.00 -- the cap is not the binding constraint.

  HEADLINE = worker_cpu_total / worker_kernel_cpu_total + completions_per_dequeue.
  NOT net_cpu: at CC4000 the IOCP workers are only ~17% of net CPU (0.31 of 1.78 cores);
  the other ~83% is the SendWorker pool submitting WSASend, which harvesting never touches.
  Reading this A/B on net_cpu would bury a real 20% worker saving inside a 3% net number.
  net_cpu is still collected and reported as the "what does it mean for the whole server" line.

  GATE = buffer_full==0 & tick_p99<40 & session>=CC*0.98. If |load asymmetry| > 3%,
  quote worker_cpu_per_kpps instead of the raw core count.

  Output: Monitoring\metrics_out\window_metrics.csv (RunLabel = GQEX_<CC>_<arm>_r<rep>)
          + console/transcript summary (per-CC GQCS vs GQCSEx).

  Usage:
    .\gqcs-ab.ps1                    # full: CC{2000,4000,4800} x {GQCS,EX64} x 2 reps, ~2.5h
    .\gqcs-ab.ps1 -Smoke             # pipeline check: CC2000 x both arms x1, 1min load, ~10min
    .\gqcs-ab.ps1 -ClientCounts 4000 # single load point
#>
param(
  [switch] $Smoke,
  [switch] $SkipBuild,
  [int]    $Reps          = 2,
  [int]    $LoadMin       = 10,
  [int]    $WindowMin     = 5,
  [int[]]  $ClientCounts  = @(2000, 4000, 4800),
  [int[]]  $Batches       = @(0, 64),   # 0 = GQCS baseline; must stay first
  [int]    $MaxClients    = 6000,
  [int]    $WorkerThreads = 4,          # IOCP arm WT (fixed across arms -- not a variable here)
  [int]    $SendWorkers   = 3,          # IOCP arm K (fixed; harvesting does not touch the send pool)
  [string] $ServerCores   = "0-5",
  [string] $LabelPrefix   = "GQEX"
)
$ErrorActionPreference = "Stop"
# Smoke uses its own label prefix: otherwise a smoke row and a real row land in the CSV under
# the identical RunLabel (same CC/arm/rep) and only the WindowMin filter separates them.
if ($Smoke) { $Reps = 1; $LoadMin = 1; $WindowMin = 1; $ClientCounts = @(2000); $LabelPrefix = "GQEXSMK" }

$RioDir = $PSScriptRoot
$Root   = Split-Path $RioDir -Parent
$Bin    = Join-Path $Root "Run\bin"
$Mon    = Join-Path $Root "Monitoring"
$SrvIni = Join-Path $Bin "MMOServerConfig.ini"
$CliIni = Join-Path $Bin "MMOStressConfig.ini"
$OutDir = Join-Path $Mon "metrics_out"
$Csv    = Join-Path $OutDir "window_metrics.csv"

# INI edit: CP949 (ANSI, no BOM). Line-array form on purpose -- a whole-text regex
# ("^key=.*") eats the CR of the matched line and leaves mixed line endings behind.
function Set-Ini([string]$file,[string]$key,[string]$value){
  (Get-Content -Encoding Default $file) -replace "^$key=.*","$key=$value" | Set-Content -Encoding Default $file
}
function ToNum([string]$s){ $d=0.0; if([double]::TryParse($s,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$d)){$d}else{$null} }
function Stop-Procs { foreach($n in "MMOStressClient","GameClient","MMOServer"){ try{ Stop-Process -Name $n -Force -ErrorAction Stop }catch{} } }
function ArmTag([int]$b){ if($b -eq 0){ "GQCS" } else { "EX$b" } }

if(-not (Test-Path $OutDir)){ New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
Start-Transcript -Path (Join-Path $OutDir ("gqcs_ab_{0}.log" -f (Get-Date -Format "yyyyMMdd_HHmmss"))) | Out-Null

$configs = @()
foreach($cc in $ClientCounts){ foreach($b in $Batches){ $configs += @{ cc=$cc; batch=$b; tag=(ArmTag $b) } } }
$total = $configs.Count * $Reps

Write-Host "=== GQCS vs GQCSEx : $($configs.Count) configs x $Reps reps = $total runs | Load=${LoadMin}m Win=${WindowMin}m WT$WorkerThreads+K$SendWorkers Cores=$ServerCores ===" -ForegroundColor Cyan
$configs | ForEach-Object { Write-Host ("   - CC{0} {1} (CompletionBatch={2})" -f $_.cc,$_.tag,$_.batch) }

$srvBak = [IO.File]::ReadAllBytes($SrvIni)
$cliBak = [IO.File]::ReadAllBytes($CliIni)

try {
  # ---- 0) clean start ----
  Stop-Procs
  foreach($n in "prometheus","windows_exporter","grafana-server"){ try{ Stop-Process -Name $n -Force -ErrorAction Stop }catch{} }

  # ---- 1) guarantee the IOCP arm binary (both arms share it; RIO build has no GQCS path) ----
  if(-not $SkipBuild){
    Write-Host ">>> REBUILD arm=A IOCP (both arms run on this one binary)" -ForegroundColor Magenta
    & cmd /c "`"$(Join-Path $RioDir 'build-A-iocp.bat')`""
    if($LASTEXITCODE -ne 0){ throw "rebuild failed (exit=$LASTEXITCODE)" }
  }
  $exe = Join-Path $Bin "MMOServer.exe"
  Write-Host ("    binary: {0}  {1:yyyy-MM-dd HH:mm:ss}  {2} bytes" -f (Split-Path $exe -Leaf), (Get-Item $exe).LastWriteTime, (Get-Item $exe).Length) -ForegroundColor DarkGray

  # ---- 2) fixed common INI (identical for every run -- only CC and CompletionBatch move) ----
  Set-Ini $SrvIni "Mode" "GameServer"
  Set-Ini $SrvIni "MonitorEnabled" "1"
  Set-Ini $SrvIni "ServerCores"   $ServerCores
  Set-Ini $SrvIni "WorkerThreads" $WorkerThreads
  Set-Ini $SrvIni "SendWorkers"   $SendWorkers
  Set-Ini $SrvIni "MaxClients"    $MaxClients
  Set-Ini $SrvIni "GameCore"      ""
  Set-Ini $CliIni "ServerIp"      "127.0.0.1"

  # ---- 3) monitoring stack (once for the whole sweep) ----
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Mon "config\setup.ps1") -StressClientIp localhost
  if($LASTEXITCODE -ne 0){ throw "setup.ps1 (prometheus target inject) failed" }
  Start-Process -FilePath (Join-Path $Mon "windows_exporter.exe")
  Start-Process -FilePath (Join-Path $Mon "prometheus-3.4.1.windows-amd64\prometheus.exe") `
                -WorkingDirectory (Join-Path $Mon "prometheus-3.4.1.windows-amd64") -ArgumentList "--web.listen-address=:9091"
  Start-Process -FilePath (Join-Path $Mon "grafana\bin\grafana-server.exe") -WorkingDirectory (Join-Path $Mon "grafana\bin")
  Start-Sleep 5

  # ---- 4) main loop: no rebuild between arms -- CompletionBatch is a runtime INI key ----
  $done = 0
  foreach($cfg in $configs){
    Set-Ini $SrvIni "CompletionBatch" $cfg.batch
    Set-Ini $CliIni "ClientCount"     $cfg.cc
    # keep 4 client threads (ClientCores 6-9) -> no client-side oversubscription
    Set-Ini $CliIni "ClientsPerThread" ([string][int][math]::Ceiling($cfg.cc/4.0))

    for($rep=1; $rep -le $Reps; $rep++){
      $done++
      $label = "{0}_{1}_{2}_r{3}" -f $LabelPrefix,$cfg.cc,$cfg.tag,$rep
      Write-Host ""
      Write-Host "[$done/$total] $label  ($(Get-Date -Format 'HH:mm:ss'))" -ForegroundColor Yellow
      Stop-Procs; Start-Sleep 3
      Start-Process -FilePath $exe -WorkingDirectory $Bin
      $ready=$false
      for($i=0;$i -lt 30;$i++){ if(Get-NetTCPConnection -State Listen -LocalPort 6000 -ErrorAction SilentlyContinue){$ready=$true;break}; Start-Sleep 1 }
      if(-not $ready){ throw "server did not listen on :6000 in 30s ($label)" }
      Start-Process -FilePath (Join-Path $Bin "MMOStressClient.exe") -WorkingDirectory $Bin

      for($m=1;$m -le $LoadMin;$m++){ Start-Sleep 60; Write-Host ("    load {0}/{1}m" -f $m,$LoadMin) }

      if(Get-Process MMOServer -ErrorAction SilentlyContinue){
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Mon "metrics-collect.ps1") `
            -RunLabel $label -WindowMin $WindowMin -QueriesFile (Join-Path $Mon "queries.json") -OutDir $OutDir
        if($LASTEXITCODE -ne 0){ Write-Warning "collect failed: $label (continue)" }

        # arm assert: the running binary must report the harvest mode we think we set.
        # Same-binary A/B means a silently unapplied INI would otherwise be invisible.
        $rows = Import-Csv $Csv
        $gotB = ($rows | Where-Object { $_.RunLabel -eq $label -and $_.Metric -eq 'completion_batch' } | Select-Object -Last 1).Value
        if($null -ne $gotB -and [int]$gotB -ne $cfg.batch){
          throw "ARM MISLABEL: $label expected CompletionBatch=$($cfg.batch) but server reports $gotB -- INI not applied, aborting to avoid corrupt data"
        }
        $gotT = ($rows | Where-Object { $_.RunLabel -eq $label -and $_.Metric -eq 'transport_rio' } | Select-Object -Last 1).Value
        if($null -ne $gotT -and [int]$gotT -ne 0){
          throw "TRANSPORT MISLABEL: $label is a RIO binary (transport_rio=$gotT) -- GQCS/GQCSEx path is not compiled in it"
        }
      } else { Write-Warning "server died during load -> skip collect: $label" }

      Stop-Procs; Start-Sleep 60   # TIME_WAIT / socket drain rest
    }
  }

  # ---- 5) summary: per-CC GQCS vs GQCSEx ----
  Write-Host ""
  Write-Host "=== SUMMARY (avg across $Reps reps) ===" -ForegroundColor Green
  if(Test-Path $Csv){
    $rows = Import-Csv $Csv
    function Avg($cc,$tag,$metric){
      $vals = for($r=1;$r -le $Reps;$r++){
        ToNum (($rows | Where-Object { $_.RunLabel -eq ("{0}_{1}_{2}_r{3}" -f $LabelPrefix,$cc,$tag,$r) -and $_.Metric -eq $metric -and [int]$_.WindowMin -eq $WindowMin } | Select-Object -Last 1).Value)
      }
      $vals = @($vals | Where-Object { $_ -ne $null })
      if($vals.Count){ ($vals | Measure-Object -Average).Average } else { $null }
    }
    $keys = "worker_cpu_total","worker_kernel_cpu_total","completions_per_dequeue","worker_dequeue_rate","worker_completions_rate",
            "worker_cpu_per_kpps","worker_kernel_per_kpps","net_cpu_total","net_kernel_cpu_total","sendworker_cpu_total",
            "gameloop_cpu","tick_p99_ms","dummy_loop_p99_ms","dummy_send_buffer_full_rate","dummy_send_pps","session_count"

    $tbl = foreach($cfg in $configs){
      $o = [ordered]@{ CC=$cfg.cc; Arm=$cfg.tag }
      foreach($k in $keys){ $v=Avg $cfg.cc $cfg.tag $k; $o[$k]= if($v -ne $null){[math]::Round($v,4)}else{"NA"} }
      $bf=Avg $cfg.cc $cfg.tag "dummy_send_buffer_full_rate"; $tp=Avg $cfg.cc $cfg.tag "tick_p99_ms"; $sc=Avg $cfg.cc $cfg.tag "session_count"
      $o["GATE"]= if(($bf -ne $null -and $bf -eq 0) -and ($tp -ne $null -and $tp -lt 40) -and ($sc -ne $null -and $sc -ge $cfg.cc*0.98)){"PASS"}else{"FAIL"}
      [pscustomobject]$o
    }
    $tbl | Format-Table CC,Arm,worker_cpu_total,worker_kernel_cpu_total,completions_per_dequeue,worker_dequeue_rate,net_cpu_total,tick_p99_ms,dummy_send_pps,session_count,GATE -AutoSize

    Write-Host ""
    Write-Host "=== VERDICT per load point (baseline = GQCS) ===" -ForegroundColor Cyan
    $baseTag = ArmTag $Batches[0]
    foreach($cc in $ClientCounts){
      $base = $tbl | Where-Object { $_.CC -eq $cc -and $_.Arm -eq $baseTag } | Select-Object -First 1
      if(-not $base){ Write-Warning "CC${cc}: no baseline row"; continue }
      foreach($b in $Batches){
        if($b -eq $Batches[0]){ continue }
        $arm = $tbl | Where-Object { $_.CC -eq $cc -and $_.Arm -eq (ArmTag $b) } | Select-Object -First 1
        if(-not $arm){ continue }
        if($base.GATE -ne "PASS" -or $arm.GATE -ne "PASS"){ Write-Warning ("CC{0} {1}: gate FAIL (base={2} arm={3}) -- numbers below are not decision-grade" -f $cc,(ArmTag $b),$base.GATE,$arm.GATE) }

        $pctOf = { param($a,$bv) if($a -ne "NA" -and $bv -ne "NA" -and [double]$a -ne 0){ ([double]$bv-[double]$a)/[double]$a*100 } else { $null } }
        $pW = & $pctOf $base.worker_cpu_total        $arm.worker_cpu_total
        $pK = & $pctOf $base.worker_kernel_cpu_total $arm.worker_kernel_cpu_total
        $pN = & $pctOf $base.net_cpu_total           $arm.net_cpu_total
        $pP = & $pctOf $base.dummy_send_pps          $arm.dummy_send_pps
        $pWn= & $pctOf $base.worker_cpu_per_kpps     $arm.worker_cpu_per_kpps

        Write-Host ("CC{0}  GQCS -> {1}" -f $cc,(ArmTag $b)) -ForegroundColor Yellow
        Write-Host ("   batch efficiency  {0,-12} -> {1,-12}  (completions per harvest call; 1.0 = no batching)" -f $base.completions_per_dequeue,$arm.completions_per_dequeue)
        Write-Host ("   harvest calls/s   {0,-12} -> {1,-12}" -f $base.worker_dequeue_rate,$arm.worker_dequeue_rate)
        Write-Host ("   WORKER cpu        {0,-12} -> {1,-12}  ({2:+0.0;-0.0}%)   <- headline" -f $base.worker_cpu_total,$arm.worker_cpu_total,$pW)
        Write-Host ("   worker KERNEL     {0,-12} -> {1,-12}  ({2:+0.0;-0.0}%)   <- syscall burden" -f $base.worker_kernel_cpu_total,$arm.worker_kernel_cpu_total,$pK)
        Write-Host ("   net cpu (server)  {0,-12} -> {1,-12}  ({2:+0.0;-0.0}%)   <- diluted by SendWorker pool" -f $base.net_cpu_total,$arm.net_cpu_total,$pN)
        Write-Host ("   load (pps)        {0,-12} -> {1,-12}  ({2:+0.0;-0.0}%)" -f $base.dummy_send_pps,$arm.dummy_send_pps,$pP)
        if($pP -ne $null -and [math]::Abs($pP) -gt 3){
          Write-Host ("   !! load asymmetry >3% -- use normalized: worker_cpu_per_kpps {0} -> {1} ({2:+0.0;-0.0}%)" -f $base.worker_cpu_per_kpps,$arm.worker_cpu_per_kpps,$pWn) -ForegroundColor Yellow
        }

        # Interpretation line -- the likely outcome is "nothing to batch", and that reading is
        # easy to mistake for "the experiment failed". It is an answer, not a failure:
        # batching needs a queue, a queue needs the workers to be behind, and at this load
        # they are not (see worker_cpu vs 1.0 core). Say so explicitly in the transcript.
        $eff = if($arm.completions_per_dequeue -ne "NA"){ [double]$arm.completions_per_dequeue } else { $null }
        if($eff -ne $null -and $eff -lt 1.05){
          $util = if($arm.worker_cpu_total -ne "NA"){ [math]::Round([double]$arm.worker_cpu_total / $WorkerThreads * 100,1) } else { "?" }
          Write-Host ("   => NO BATCHING at CC{0} (eff {1:N3}). Workers are never behind (~{2}% busy each), so the" -f $cc,$eff,$util) -ForegroundColor DarkCyan
          Write-Host ("      queue is empty on arrival and GQCSEx has nothing to coalesce. Not a bug -- a load answer.") -ForegroundColor DarkCyan
        } elseif($eff -ne $null) {
          Write-Host ("   => batching active at CC{0}: {1:N2} completions per call, harvest syscalls cut {2:N0}%" -f $cc,$eff,((1-1/$eff)*100)) -ForegroundColor DarkCyan
        }
      }
    }
    Write-Host ""
    Write-Host "raw metrics: $Csv" -ForegroundColor DarkGray
  } else { Write-Warning "no CSV produced (collection never succeeded)" }
}
finally {
  Stop-Procs
  [IO.File]::WriteAllBytes($SrvIni,$srvBak)
  [IO.File]::WriteAllBytes($CliIni,$cliBak)
  Write-Host "INIs restored to original bytes" -ForegroundColor DarkYellow
  Stop-Transcript | Out-Null
}
