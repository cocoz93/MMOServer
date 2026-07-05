# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF4 = final resolution (high-res for PPT, Pretendard embedded):
#   01 : 1140x640  -> 4560x2560   (01_paradox_833.png)
#   02 : 1200x600  -> 4800x2400   (02_mechanism_sendq.png)
#   03 : 1200x690  -> 4800x2760   (03_verdict_wan474.png)
#   04 : 1180x620  -> 4720x2480   (04_crosscheck_3axis.png)
#   05 : 1140x620  -> 4560x2480   (05_core_isolation.png)
#   06 : 1320x720  -> 5280x2880   (06_before_after.png)   [콜아웃 머메이드 대체: 변경 전/후 통합, 가로 2행 간략·큼직, 덱 양식]
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

# Render inside an ASCII temp dir. The source folder name has spaces + Korean, which:
#   (1) makes the file:// URL / --screenshot arg word-split under Start-Process ("Multiple targets" error), and
#   (2) chrome.exe is a GUI-subsystem app, so PowerShell '&' does NOT wait for the render to finish.
# A fresh per-run profile (GUID) prevents attaching to a running Chrome / stale instance (screenshot no-op).
$render = Join-Path $env:TEMP "ig-render5"
$profileDir = Join-Path $env:TEMP ("ig-prof5-" + [Guid]::NewGuid().ToString('N').Substring(0,8))
if (Test-Path $render) { Remove-Item $render -Recurse -Force }
New-Item -ItemType Directory -Path $render -Force | Out-Null
Copy-Item (Join-Path $dir "*.html") $render -Force
Copy-Item (Join-Path $dir "fonts")  $render -Recurse -Force

function Render($name, $w, $h, $out) {
  $png = Join-Path $render "$name.png"
  if (Test-Path $png) { Remove-Item $png -Force }
  $url = "file:///" + ($render -replace '\\','/') + "/$name.html"
  $chromeArgs = @(
    '--headless=new','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files',
    "--user-data-dir=$profileDir",'--force-device-scale-factor=4',
    "--screenshot=$png","--window-size=$w,$h",$url
  )
  # Start-Process -Wait: synchronous, so the PNG exists before we copy it out.
  Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -Wait
  if (-not (Test-Path $png)) { throw "render failed: $name (no screenshot produced)" }
  Copy-Item $png (Join-Path $dir "..\$out") -Force
  Write-Host "  built  ..\$out  (logical ${w}x${h}, 4x output)"
}

Write-Host "[build] using: $chrome"
Render "01" 1140 640 "01_paradox_833.png"
Render "02" 1200 600 "02_mechanism_sendq.png"
Render "03" 1200 690 "03_verdict_wan474.png"
Render "04" 1180 620 "04_crosscheck_3axis.png"
Render "05" 1140 620 "05_core_isolation.png"
Render "06" 1320 720 "06_before_after.png"
Remove-Item $render -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "[build] done."
