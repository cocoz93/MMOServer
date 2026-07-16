# Rebuild infographic PNGs from the HTML sources (PowerShell version).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF4 = final resolution (high-res for PPT, Pretendard embedded):
#   01 : 1000x640  -> 4000x2560   (01_diagnosis.png)
#   02 : 1220x620  -> 4880x2480   (02_structure.png)
#   03 : 1220x640  -> 4880x2560   (03_shift.png)
#   04 : 1120x680  -> 4480x2720   (04_matrix.png)
#   05 : 1180x640  -> 4720x2560   (05_conclusion.png)
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

# Render inside an ASCII temp dir. The source folder name has spaces + Korean, which:
#   (1) makes the file:// URL / --screenshot arg word-split under Start-Process ("Multiple targets" error), and
#   (2) chrome.exe is a GUI-subsystem app, so PowerShell '&' does NOT wait for the render to finish.
# A fresh per-run profile (GUID) prevents attaching to a running Chrome / stale instance (screenshot no-op).
$render = Join-Path $env:TEMP "ig-render7"
$profileDir = Join-Path $env:TEMP ("ig-prof-" + [Guid]::NewGuid().ToString('N').Substring(0,8))
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
Render "01" 1000 640 "01_diagnosis.png"
Render "02" 1240 652 "02_structure.png"
Render "03" 1240 582 "03_shift.png"
Render "04" 1140 706 "04_matrix.png"
Render "05" 1200 552 "05_conclusion.png"
Remove-Item $render -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "[build] done."
