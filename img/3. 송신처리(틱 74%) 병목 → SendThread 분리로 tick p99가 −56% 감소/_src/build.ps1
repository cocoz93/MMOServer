# SendThread 분리 인포그래픽 PNG 빌드 (PowerShell)
# 사용법:  powershell -ExecutionPolicy Bypass -File build.ps1
# 논리 1200x700 x DSF3 -> 3600x2100 (PPT용 고해상, Pretendard 임베드)
#
# 주의: 이 폴더 경로에는 유니코드(→, −)·괄호·%가 들어 있어 Chrome CLI가
#       file URL / --screenshot 경로를 직접 처리하지 못한다.
#       그래서 ASCII 임시폴더($env:TEMP)에 소스를 복사해 렌더한 뒤,
#       결과 PNG만 이 폴더로 되돌려 복사한다. (--user-data-dir 지정 금지 — headless 스샷 실패함)
$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$dst = Split-Path $src -Parent

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

$work = Join-Path $env:TEMP 'render_sendthread_p99'
New-Item -ItemType Directory -Force "$work\fonts" | Out-Null
Copy-Item "$src\*.html"   $work -Force
Copy-Item "$src\common.css" $work -Force
Copy-Item "$src\fonts\*"  "$work\fonts\" -Force

function Render($name) {
  $png = "$work\$name.png"
  $url = "file:///" + ($work -replace '\\','/') + "/$name.html"
  $a = @('--headless','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files',
         '--force-device-scale-factor=3',"--screenshot=$png",'--window-size=1200,700',$url)
  Start-Process -FilePath $chrome -ArgumentList $a -Wait -NoNewWindow | Out-Null
  $out = Join-Path $dst "$name.png"
  Copy-Item $png $out -Force
  Write-Host ("  built  ..\$name.png  (" + [math]::Round((Get-Item $out).Length/1kb) + " KB)")
}

Write-Host "[build] using: $chrome"
Render "01_diag_tick74"
Render "02_result_p99"
Render "03_cpu_no_shift"
Render "04_hero"
Write-Host "[build] done."
