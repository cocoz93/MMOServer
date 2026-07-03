# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF3 = final resolution (high-res for PPT, Pretendard embedded):
#   01 : 1200x760 -> 3600x2280   (01_session_array_sessionid.png)
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
Render "01" 1200 760 "01_session_array_sessionid.png"
Write-Host "[build] done."
