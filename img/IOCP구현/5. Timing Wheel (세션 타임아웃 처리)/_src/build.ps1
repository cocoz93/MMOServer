# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF3 = final resolution (high-res for PPT, Pretendard embedded):
#   01 : 1200x600 -> 3600x1800   (01_scan_vs_wheel.png)
#   02 : 1200x600 -> 3600x1800   (02_slot_plus_one.png)
#   03 : 940x680  -> 2820x2040   (03_tick_resolution.png)
#   04 : 1160x560 -> 3480x1680   (04_aba_guard.png)
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }
# NOTE: a dedicated --user-data-dir is required — if a normal Chrome window is
# already running, a plain "chrome.exe --headless" forwards to that existing
# process and exits immediately without producing a screenshot.
$renderProfile = Join-Path $env:TEMP "chrome-headless-render"

function Render($name, $w, $h, $out) {
  $png = Join-Path $dir "$name.png"
  if (Test-Path $png) { Remove-Item $png -Force }
  $url = "file:///" + ($dir -replace '\\','/') + "/$name.html"
  & $chrome --headless --disable-gpu --hide-scrollbars --allow-file-access-from-files --force-device-scale-factor=3 "--user-data-dir=$renderProfile" "--screenshot=$png" "--window-size=$w,$h" $url
  # chrome.exe can return before the screenshot file finishes flushing to disk
  $waited = 0
  while (-not (Test-Path $png) -and $waited -lt 10000) { Start-Sleep -Milliseconds 200; $waited += 200 }
  if (-not (Test-Path $png)) { throw "screenshot not produced: $name.html" }
  Copy-Item $png (Join-Path $dir "..\$out") -Force
  Write-Host "  built  ..\$out  (logical ${w}x${h}, 3x output)"
}

Write-Host "[build] using: $chrome"
Render "01" 1200 600 "01_scan_vs_wheel.png"
Render "02" 1200 600 "02_slot_plus_one.png"
Render "03" 940 680 "03_tick_resolution.png"
Render "04" 1160 560 "04_aba_guard.png"
Write-Host "[build] done."
