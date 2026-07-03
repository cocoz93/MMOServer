# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Use this if you prefer PowerShell over: bash build.sh
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF3 = final resolution (high-res for PPT, Pretendard embedded):
#   01_v4 : 1520x800 -> 4560x2400 (01_accept_thread_separation.png)
#   (2026-07-01: redesigned as left/right circle-contrast panels)
#   (2026-07-02: decluttered — panel bg/VS badge/gap 라벨 제거, cluster 정렬 고정)
#   (2026-07-02: v4 — IOCP 내부 단계(accept→IOCP등록→WSARecv게시→GQCS) 노드화 +
#                배경 도트 텍스처/그라디언트 커넥터/2단 그림자/타이포 위계로 실행품질 상향.
#                실제 소스는 01_v4.html (구버전 01.html·concept_C_unified.html 은 참고용 보존))
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$profileDir = Join-Path $env:TEMP "mmo-infographic-chrome-profile"   # 실행 중인 일반 Chrome 프로필과 충돌 방지

# 이 저장소 폴더명의 한글/공백/괄호가 chrome.exe 인자로 넘어가면 "Multiple targets" 오류 또는
# 잘못된 경로로 깨짐 (PowerShell 인자 마샬링 문제) -> ASCII 전용 임시 폴더에 스테이징 후 렌더링
$stageRoot = Join-Path $env:TEMP "mmo-infographic-stage"

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
  Write-Host "  built  ..\$out  (logical ${w}x${h}, 3x output)"
}

Write-Host "[build] using: $chrome"
Render "01_v4" 1520 800 "01_accept_thread_separation.png"
Write-Host "[build] done."
