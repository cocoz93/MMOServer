# CAREER 타임라인 인포그래픽 렌더 (하우스 톤, PPT급)
# 1600x900 logical x DSF3 = 4800x2700 최종 + 1x 미리보기
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$profileDir = Join-Path $env:TEMP "mmo-infographic-chrome-profile"
$stageRoot  = Join-Path $env:TEMP "mmo-timeline-stage"

function Render($name, $w, $h, $dsf, $outName) {
  $stage = Join-Path $stageRoot "$name-$dsf"
  if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  Copy-Item (Join-Path $dir "$name.html") (Join-Path $stage "$name.html")
  Copy-Item (Join-Path $dir "fonts") (Join-Path $stage "fonts") -Recurse

  $stagePng = Join-Path $stage "$outName.png"
  $url = "file:///" + (($stage -replace '\\','/')) + "/$name.html"
  $chromeArgs = @(
    "--headless=new","--disable-gpu","--hide-scrollbars","--allow-file-access-from-files",
    "--force-device-scale-factor=$dsf","--user-data-dir=$profileDir",
    "--screenshot=$stagePng","--window-size=$w,$h",$url
  )
  $p = Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -PassThru -Wait
  if ($p.ExitCode -ne 0 -or -not (Test-Path $stagePng)) { throw "Render failed for $name (exit $($p.ExitCode))" }

  Copy-Item $stagePng (Join-Path $dir "$outName.png") -Force
  Copy-Item $stagePng (Join-Path $dir "..\$outName.png") -Force
  Write-Host "  built  ..\$outName.png  ($($w*$dsf)x$($h*$dsf))"
}

Write-Host "[timeline] using: $chrome"
Render "deck_timeline" 1600 900 3 "career_timeline"        # 4800x2700 최종
Render "deck_timeline" 1600 900 1 "career_timeline_1x"     # 1600x900 미리보기
Write-Host "[timeline] done."