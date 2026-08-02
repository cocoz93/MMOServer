<#
.SYNOPSIS
    One-shot A/B metric collector. Queries Prometheus over a time window and
    writes a flat row per metric -- NO double snapshot, NO manual rate math.

.DESCRIPTION
    Prometheus already stores every series at scrape resolution. This script
    asks it to slice the last -WindowMin minutes (rate / histogram_quantile /
    avg_over_time, defined in queries.json) and emits one value per metric.

    Run ONCE after load has been stable for >= WindowMin minutes.

.EXAMPLE
    .\query_window.ps1 -RunLabel A_baseline
    .\query_window.ps1 -RunLabel B_coalescing -WindowMin 5
    .\query_window.ps1 -RunLabel soak -WindowMin 10 -PromUrl http://localhost:9091
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $RunLabel,

    # rate/quantile lookback window, in minutes (default 5 = ~60 samples @5s scrape)
    [int]    $WindowMin = 5,

    # Prometheus base URL (NOT the server :9090 exposition -- the TSDB on :9091)
    [string] $PromUrl = "http://localhost:9091",

    # evaluation instant (default = now). Accepts a parseable datetime for replay.
    [string] $At = "",

    [string] $QueriesFile = (Join-Path $PSScriptRoot "queries.json"),
    [string] $OutDir      = (Join-Path $PSScriptRoot "metrics_out")
)

# -- load query set --
if (-not (Test-Path $QueriesFile)) {
    Write-Error "Query set not found: $QueriesFile"; exit 1
}
$queries = Get-Content -Raw -Path $QueriesFile | ConvertFrom-Json

# -- resolve evaluation instant -> unix seconds --
if ([string]::IsNullOrWhiteSpace($At)) { $evalTime = Get-Date }
else {
    try { $evalTime = [datetime]::Parse($At) }
    catch { Write-Error "Cannot parse -At '$At'"; exit 1 }
}
$evalUnix = ([DateTimeOffset]$evalTime.ToUniversalTime()).ToUnixTimeSeconds()
$tsUtc = $evalTime.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
$window = "${WindowMin}m"

# -- query Prometheus per metric --
$rows = New-Object System.Collections.Generic.List[object]
foreach ($q in $queries) {
    $expr = $q.promql -replace '\$W', $window
    $uri  = "$PromUrl/api/v1/query?query=$([uri]::EscapeDataString($expr))&time=$evalUnix"

    # -- network call: only a transport failure aborts the whole run --
    try {
        $resp = Invoke-RestMethod -Uri $uri -TimeoutSec 15 -ErrorAction Stop
    }
    catch {
        Write-Error "Cannot reach Prometheus at $PromUrl. Up? ($($_.Exception.Message))"
        exit 1
    }

    # -- parse value: a bad/non-finite value never aborts, just yields null --
    $value = $null
    if ($resp.status -ne "success") {
        Write-Warning "[$($q.name)] query status=$($resp.status)"
    }
    elseif ($resp.data.result.Count -eq 0) {
        Write-Warning "[$($q.name)] empty result (window has no data? Prometheus up < ${WindowMin}m?)"
    }
    else {
        if ($resp.data.result.Count -gt 1) {
            Write-Warning "[$($q.name)] returned $($resp.data.result.Count) series; using first"
        }
        $raw = [string]$resp.data.result[0].value[1]   # e.g. "1234.5", "+Inf", "NaN"
        $d = 0.0
        $parsed = [double]::TryParse($raw, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$d)
        if ($parsed -and -not ([double]::IsNaN($d) -or [double]::IsInfinity($d))) {
            $value = [math]::Round($d, 4)
        }
        else {
            Write-Warning "[$($q.name)] non-finite/unparseable value '$raw' -> null"
        }
    }

    $role = if ($q.role) { $q.role } else { "kpi" }
    $rows.Add([pscustomobject]@{
        TimeUtc        = $tsUtc
        RunLabel       = $RunLabel
        WindowMin      = $WindowMin
        Metric         = $q.name
        Value          = $value
        Unit           = $q.unit
        Role           = $role
        LowerIsBetter  = $q.lower_is_better
    })
}

# -- persist (shared CSV, one row per metric per run) --
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$csvPath = Join-Path $OutDir "window_metrics.csv"

# schema guard: PS 5.1 Export-Csv -Append silently DROPS columns absent from the
# existing header. If the schema drifted (new column/metric), rotate the old file
# aside so we never half-write rows or lose a column without notice.
if (Test-Path $csvPath) {
    $hdr          = (Get-Content -Path $csvPath -TotalCount 1) -replace '"', ''
    $existingCols = ($hdr -split ',').Trim().TrimStart([char]0xFEFF)
    $newCols      = $rows[0].PSObject.Properties.Name
    if (Compare-Object $existingCols $newCols) {
        $bak = Join-Path $OutDir ("window_metrics_{0}.bak.csv" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
        Move-Item -Path $csvPath -Destination $bak
        Write-Warning "CSV schema changed -> rotated old file to $bak (fresh file started)"
    }
}
$rows | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8 -Append

# -- console view --
Write-Host ""
Write-Host "=== $RunLabel  (window=${window}, eval=$tsUtc) ===" -ForegroundColor Cyan
$rows | Format-Table Metric, Value, Unit -AutoSize
Write-Host "CSV -> $csvPath (appended $($rows.Count) metrics)" -ForegroundColor Green
