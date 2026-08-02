<#
.SYNOPSIS
    Compare two RunLabels from window_metrics.csv into a side-by-side delta table.

.EXAMPLE
    .\compare.ps1 -Baseline A_baseline -Variant B_coalescing
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Baseline,
    [Parameter(Mandatory = $true)] [string] $Variant,
    [string] $CsvPath = (Join-Path $PSScriptRoot "metrics_out\window_metrics.csv")
)

if (-not (Test-Path $CsvPath)) { Write-Error "Not found: $CsvPath"; exit 1 }
$all = Import-Csv -Path $CsvPath

# latest row per (RunLabel, Metric) -- last write wins
function Latest($label) {
    $rows = $all | Where-Object RunLabel -eq $label
    if (-not $rows) { Write-Error "RunLabel '$label' not in CSV"; exit 1 }
    $map = @{}
    foreach ($r in $rows) { $map[$r.Metric] = $r }   # CSV is append-order
    return $map
}
$base = Latest $Baseline
$var  = Latest $Variant

# parse CSV string -> double (InvariantCulture); null if empty/unparseable
function ToNum($s) {
    $d = 0.0
    if ([double]::TryParse([string]$s, [Globalization.NumberStyles]::Float,
                           [Globalization.CultureInfo]::InvariantCulture, [ref]$d)) { $d } else { $null }
}

$out = foreach ($metric in ($base.Keys | Sort-Object)) {
    $b = $base[$metric].Value
    $v = if ($var.ContainsKey($metric)) { $var[$metric].Value } else { $null }
    $bn = ToNum $b
    $vn = ToNum $v
    $isControl = ($base[$metric].Role -eq "control")
    $delta = ""
    $verdict = ""
    if ($null -ne $bn -and $null -ne $vn) {
        if ($bn -eq 0) {
            # baseline 0 -> %change undefined; flag by direction instead of blanking.
            if ($vn -eq 0) {
                $delta = "0"; $verdict = if ($isControl) { "=" } else { "~" }
            }
            else {
                $delta = "+{0} (from 0)" -f [math]::Round($vn, 4)
                if ($isControl) { $verdict = "DRIFT!" }
                else { $verdict = if ([bool]::Parse($base[$metric].LowerIsBetter)) { "BAD" } else { "good" } }
            }
        }
        else {
            $pct = (($vn - $bn) / $bn) * 100
            $delta = "{0:+0.0;-0.0}%" -f $pct
            if ($isControl) {
                # 통제변수: 방향 무관, "안 변해야 정상". 3% 밴드 이탈 시 오염 경고.
                $verdict = if ([math]::Abs($pct) -lt 3) { "=" } else { "DRIFT!" }
            }
            else {
                # KPI: 개선 방향(lower_is_better)과 부호 일치 여부로 판정
                $lib = [bool]::Parse($base[$metric].LowerIsBetter)
                if ([math]::Abs($pct) -lt 1) { $verdict = "~" }
                elseif (($pct -lt 0) -eq $lib) { $verdict = "good" }
                else { $verdict = "BAD" }
            }
        }
    }
    [pscustomobject]@{
        Metric   = $metric
        Baseline = $b
        Variant  = $v
        Delta    = $delta
        Verdict  = $verdict
        Unit     = $base[$metric].Unit
    }
}

Write-Host ""
Write-Host "=== $Baseline  ->  $Variant ===" -ForegroundColor Cyan
$out | Format-Table Metric, Baseline, Variant, Delta, Verdict, Unit -AutoSize
