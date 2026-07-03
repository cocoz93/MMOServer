# 하우스 톤(라이트+레드/블루, SendQ 폴더 스킨) 시안 렌더링
# 1600x900 logical x DSF3 = 4800x2700 최종
# Usage:  powershell -ExecutionPolicy Bypass -File build_styles_house.ps1
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$profileDir = Join-Path $env:TEMP "mmo-infographic-chrome-profile"
$stageRoot = Join-Path $env:TEMP "mmo-style-stage"

function Render($name, $w, $h, $out) {
  $stage = Join-Path $stageRoot $name
  if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  Copy-Item (Join-Path $dir "$name.html") (Join-Path $stage "$name.html")
  Copy-Item (Join-Path $dir "fonts") (Join-Path $stage "fonts") -Recurse

  $stagePng = Join-Path $stage "$name.png"
  $url = "file:///" + (($stage -replace '\\','/')) + "/$name.html"
  $chromeArgs = @(
    "--headless=new","--disable-gpu","--hide-scrollbars","--allow-file-access-from-files",
    "--force-device-scale-factor=3","--user-data-dir=$profileDir",
    "--screenshot=$stagePng","--window-size=$w,$h",$url
  )
  $p = Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -PassThru -Wait
  if ($p.ExitCode -ne 0 -or -not (Test-Path $stagePng)) { throw "Render failed for $name (exit $($p.ExitCode))" }

  Copy-Item $stagePng (Join-Path $dir "$name.png") -Force
  Copy-Item $stagePng (Join-Path $dir "..\$out") -Force
  Write-Host "  built  ..\$out  (logical ${w}x${h}, 3x)"
}

Write-Host "[build] using: $chrome"
Render "house_matrix" 1600 900 "house_matrix.png"
Render "house_hero"   1600 900 "house_hero.png"
Render "house_flow"   1600 900 "house_flow.png"
Write-Host "[build] done."
