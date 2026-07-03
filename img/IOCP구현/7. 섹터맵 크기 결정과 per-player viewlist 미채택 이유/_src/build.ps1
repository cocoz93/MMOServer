# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size 1200x700 x DSF3 = 3600x2100 (Pretendard embedded, PPT-grade hi-res)
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }
$renderProfile = Join-Path $env:TEMP "chrome-headless-render-sector7"
if (Test-Path $renderProfile) { Remove-Item -Recurse -Force $renderProfile }

function Render($name, $out) {
  $png = Join-Path $dir "$name.png"
  if (Test-Path $png) { Remove-Item $png -Force }
  $url = "file:///" + ($dir -replace '\\','/') + "/$name.html"
  $profileDir = "$renderProfile-$name"
  if (Test-Path $profileDir) { Remove-Item -Recurse -Force $profileDir }
  & $chrome --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files --force-device-scale-factor=3 "--user-data-dir=$profileDir" "--screenshot=$png" "--window-size=1200,700" $url
  $tries = 0
  while (-not (Test-Path $png) -and $tries -lt 20) { Start-Sleep -Milliseconds 500; $tries++ }
  if (-not (Test-Path $png)) { throw "screenshot not found: $png" }
  Copy-Item $png (Join-Path $dir "..\$out") -Force
  Write-Host "  built  ..\$out"
}

Write-Host "[build] using: $chrome"
Render "01_sector_grid_aoi"   "01_sector_grid_aoi.png"
Render "02_sector_vs_view"    "02_sector_vs_view.png"
Render "03_grid_vs_viewlist"  "03_grid_vs_viewlist.png"
Render "04_grid_vs_tree"      "04_grid_vs_tree.png"
Render "05_map_size_density"  "05_map_size_density.png"
Write-Host "[build] done."
