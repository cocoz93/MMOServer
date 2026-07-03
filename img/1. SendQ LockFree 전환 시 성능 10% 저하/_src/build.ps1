# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Use this if you prefer PowerShell over: bash build.sh
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF3 = final resolution (high-res for PPT, Pretendard embedded):
#   01 : 780x560   -> 2340x1680   (01_enqueue_62ns.png)
#   02 : 940x680   -> 2820x2040   (02_ab_normalized.png)
#   03 : 1200x600  -> 3600x1800   (03_before_after.png)
#   04 : 1160x560  -> 3480x1680   (04_spsc.png)
#   05 : 1140x560  -> 3420x1680   (05_dilution.png)
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

function Render($name, $w, $h, $out) {
  $png = Join-Path $dir "$name.png"
  if (Test-Path $png) { Remove-Item $png -Force }
  $url = "file:///" + ($dir -replace '\\','/') + "/$name.html"
  & $chrome --headless --disable-gpu --hide-scrollbars --allow-file-access-from-files --force-device-scale-factor=3 "--screenshot=$png" "--window-size=$w,$h" $url
  Copy-Item $png (Join-Path $dir "..\$out") -Force
  Write-Host "  built  ..\$out  (logical ${w}x${h}, 3x output)"
}

Write-Host "[build] using: $chrome"
Render "01" 780 560 "01_enqueue_62ns.png"
Render "02" 940 680 "02_ab_normalized.png"
Render "03" 1200 600 "03_before_after.png"
Render "04" 1160 560 "04_spsc.png"
Render "05" 1140 560 "05_dilution.png"
Write-Host "[build] done."
