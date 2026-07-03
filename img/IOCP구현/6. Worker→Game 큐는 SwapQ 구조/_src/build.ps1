# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size 1200x700 x DSF3 = 3600x2100 (Pretendard embedded, PPT급 고해상도)
# NOTE: --headless=new (new headless mode) has a real Chrome compositor bug at
# --force-device-scale-factor=3 that duplicates a sliver of the card's top
# accent bar near the bottom edge. Old-style --headless (+ default-background
# -color=00000000, matching the known-good reference renders in this repo)
# does not have this bug. Keep using old --headless here.
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }
$renderProfile = Join-Path $env:TEMP "chrome-headless-render-swapq"
if (Test-Path $renderProfile) { Remove-Item -Recurse -Force $renderProfile }

function Render($name, $out) {
  $png = Join-Path $dir "$name.png"
  if (Test-Path $png) { Remove-Item $png -Force }
  $url = "file:///" + ($dir -replace '\\','/') + "/$name.html"
  $profileDir = "$renderProfile-$name"
  if (Test-Path $profileDir) { Remove-Item -Recurse -Force $profileDir }
  & $chrome --headless --disable-gpu --allow-file-access-from-files --force-device-scale-factor=3 --hide-scrollbars --default-background-color=00000000 "--user-data-dir=$profileDir" "--window-size=1200,700" "--screenshot=$png" $url
  $tries = 0
  while (-not (Test-Path $png) -and $tries -lt 20) { Start-Sleep -Milliseconds 500; $tries++ }
  if (-not (Test-Path $png)) { throw "screenshot not found: $png" }
  Copy-Item $png (Join-Path $dir "..\$out") -Force
  Write-Host "  built  ..\$out"
}

Write-Host "[build] using: $chrome"
Render "v1_01_unbounded_drain"     "v1_01_unbounded_drain.png"
Render "v1_02_cause_diagnosis"     "v1_02_cause_diagnosis.png"
Render "v1_03_two_layers"          "v1_03_two_layers.png"
Render "v1_04_swapq_before_after"  "v1_04_swapq_before_after.png"
Render "v1_05_bottleneck_shift"    "v1_05_bottleneck_shift.png"

Render "v2_01_unbounded_drain"     "v2_01_unbounded_drain.png"
Render "v2_02_cause_diagnosis"     "v2_02_cause_diagnosis.png"
Render "v2_03_two_layers"          "v2_03_two_layers.png"
Render "v2_04_swapq_before_after"  "v2_04_swapq_before_after.png"
Render "v2_05_bottleneck_shift"    "v2_05_bottleneck_shift.png"
Write-Host "[build] done."
