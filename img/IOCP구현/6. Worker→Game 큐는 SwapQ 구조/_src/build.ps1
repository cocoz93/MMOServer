# SwapQ 덱 PNG 재빌드 (PowerShell)
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# 렌더 방식: ASCII 임시폴더 경유 + --headless=new + Start-Process -Wait
#   - 소스 폴더명이 한글+공백이라 file:// URL / --screenshot 인자가 word-split 되는 문제를
#     ASCII 임시폴더(%TEMP%)로 복사해 렌더하는 것으로 회피.
#   - chrome.exe는 GUI 서브시스템이라 '&'는 완료를 기다리지 않음 → Start-Process -Wait 사용.
#   - 카드에 상단 액센트 바가 없어 --headless=new 의 DSF3 액센트 중복 버그와 무관.
#
# 논리 크기 x DSF3 = 최종 해상도:
#   deck_01_cover        1600x900  -> 4800x2700  (커버, swap_deck.css)
#   v3_01 ~ v3_05        1200x700  -> 3600x2100  (콘텐츠, common.css)
$ErrorActionPreference = 'Stop'
$src  = $PSScriptRoot
$base = Split-Path $src -Parent

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$gid = [Guid]::NewGuid().ToString('N').Substring(0,8)
$render = Join-Path $env:TEMP "swapq-render-$gid"
New-Item -ItemType Directory -Path $render -Force | Out-Null
Copy-Item (Join-Path $src "*.html") $render -Force
Copy-Item (Join-Path $src "*.css")  $render -Force
Copy-Item (Join-Path $src "fonts")  $render -Recurse -Force

function Render($name, $w, $h) {
  $png = Join-Path $render "$name.png"
  $url = "file:///" + ($render -replace '\\','/') + "/$name.html"
  $profileDir = Join-Path $env:TEMP "swapq-prof-$gid-$name"
  $chromeArgs = @('--headless=new','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files',
    "--user-data-dir=$profileDir",'--force-device-scale-factor=3','--default-background-color=00000000',
    "--screenshot=$png","--window-size=$w,$h",$url)
  Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -Wait
  if (-not (Test-Path $png)) { throw "render failed: $name" }
  Copy-Item $png (Join-Path $src  "$name.png") -Force
  Copy-Item $png (Join-Path $base "$name.png") -Force
  Write-Host "  built  $name.png  (${w}x${h} x3)"
}

Write-Host "[build] using: $chrome"
Render "deck_01_cover"          1600 900
Render "v3_01_unbounded_drain"  1200 700
Render "v3_02_cause_diagnosis"  1200 700
Render "v3_03_two_layers"       1200 700
Render "v3_04_swapq_structure"  1200 700
Render "v3_05_bottleneck_shift" 1200 700
Write-Host "[build] done."
