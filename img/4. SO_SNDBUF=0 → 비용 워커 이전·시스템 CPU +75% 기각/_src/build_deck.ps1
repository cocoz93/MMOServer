# SO_SNDBUF=0 PPT deck render (house tone, 4 slides)
# cover -> mechanism -> trap -> verdict
# 1600x900 logical x DSF3 = 4800x2700 final
# Usage:  powershell -ExecutionPolicy Bypass -File build_deck.ps1
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$profileDir = Join-Path $env:TEMP "mmo-infographic-chrome-profile"
$stageRoot = Join-Path $env:TEMP "mmo-sndbuf-deck-stage"

function Render($name, $w, $h) {
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
  Copy-Item $stagePng (Join-Path $dir "..\$name.png") -Force
  Write-Host "  built  ..\$name.png"
}

Write-Host "[deck] using: $chrome"
Render "deck_00_summary"   1140 560
Render "deck_01_trap"      1140 560
Render "deck_02_mechanism" 1140 560
Render "deck_03_verdict"   1140 560
Write-Host "[deck] done."
