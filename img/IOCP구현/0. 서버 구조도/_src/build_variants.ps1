# Render all style variants -> ../variants/*.png  (headless Chrome, 4x, Pretendard embedded).
# Usage: powershell -ExecutionPolicy Bypass -File build_variants.ps1
$dir = $PSScriptRoot
$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$render = Join-Path $env:TEMP "ig-var-render"
$profileDir = Join-Path $env:TEMP ("ig-var-prof-" + [Guid]::NewGuid().ToString('N').Substring(0,8))
if (Test-Path $render) { Remove-Item $render -Recurse -Force }
New-Item -ItemType Directory -Path $render -Force | Out-Null
Copy-Item (Join-Path $dir "v_*.html") $render -Force
Copy-Item (Join-Path $dir "fonts")  $render -Recurse -Force
$outDir = Join-Path $dir "..\variants"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

function Render($name, $w, $h) {
  $png = Join-Path $render "$name.png"
  if (Test-Path $png) { Remove-Item $png -Force }
  $url = "file:///" + ($render -replace '\\','/') + "/$name.html"
  $chromeArgs = @(
    '--headless=new','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files',
    "--user-data-dir=$profileDir",'--force-device-scale-factor=4',
    "--screenshot=$png","--window-size=$w,$h",$url
  )
  Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -Wait
  if (-not (Test-Path $png)) { throw "render failed: $name" }
  Copy-Item $png (Join-Path $outDir "$name.png") -Force
  Write-Host "  built  variants/$name.png  (${w}x${h})"
}

Write-Host "[variants] using: $chrome"
Render "v_flat_h"   1500 764
Render "v_tiers_v"  1000 1180
Render "v_hub"      1240 860
Render "v_blueprint" 1500 764
Render "v_minimal"  1440 720
Render "v_lightblue" 1500 764
Render "v_pro" 1560 860
Render "v_bold" 1600 820
Remove-Item $render -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "[variants] done."
