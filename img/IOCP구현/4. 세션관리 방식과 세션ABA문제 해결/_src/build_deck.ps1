# 세션관리/ABA PPT 덱 렌더링 (하우스 톤, 8슬라이드)
# 본편 01~04: 표지 → 관리 방식 → ABA 문제 → 해결(방어)
# 심화 05~08(토글용): 소켓ABA → CAS/좀비참조 → Late-posted IO → RST 종료
# 1600x900 logical x DSF3 = 4800x2700 최종
# Usage:  powershell -ExecutionPolicy Bypass -File build_deck.ps1
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$profileDir = Join-Path $env:TEMP "mmo-infographic-chrome-profile"
$stageRoot = Join-Path $env:TEMP "mmo-deck-stage-session"

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
Render "deck_01_cover"     1600 900
Render "deck_02_structure" 1600 900
Render "deck_03_problem"   1600 900
Render "deck_04_defense"   1600 900
Render "deck_05_socket"    1600 900
Render "deck_06_cas"       1600 900
Render "deck_07_late_io"   1600 900
Render "deck_08_rst"       1600 900
Write-Host "[deck] done."
