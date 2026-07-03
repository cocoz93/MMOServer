# Rebuild infographic PNGs from HTML sources (PowerShell, Chrome 149-safe).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF3 = final resolution (high-res for PPT, Pretendard embedded):
#   02 : 1520x824 -> 4560x2472   (02_completion_order.png)
#   03 : 1520x960 -> 4560x2880   (03_page_lock.png)
#   04 : 1520x892 -> 4560x2676   (04_one_pending_pattern.png)
#   05 : 1520x846 -> 4560x2538   (05_page_lock_basics.png)   # Page Lock concept; place before 03 in Notion
# 01_pending_limit.png : existing hero output, kept as-is (no source in this folder).
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

function Render($name, $w, $h, $out) {
  $png = Join-Path $dir "$name.png"
  if (Test-Path $png) { Remove-Item $png -Force }

  # This folder path has spaces + Korean; Start-Process -ArgumentList splits such args
  # ("Multiple targets"). So stage HTML/CSS/fonts into an ASCII-only temp dir, render
  # there, then copy the PNG back. common.css must be staged (HTML links it by relative url).
  # Start-Process -Wait is required: Chrome 149 --headless=new is async.
  $work = Join-Path $env:TEMP "mmo-render-work"
  if (Test-Path $work) { Remove-Item $work -Recurse -Force }
  New-Item -ItemType Directory -Force (Join-Path $work "fonts") | Out-Null
  Copy-Item (Join-Path $dir "$name.html") (Join-Path $work "in.html")     -Force
  Copy-Item (Join-Path $dir "common.css") (Join-Path $work "common.css")  -Force
  Copy-Item (Join-Path $dir "fonts\*")    (Join-Path $work "fonts")       -Force -Recurse
  $wpng = Join-Path $work "out.png"
  $wurl = "file:///" + ($work -replace '\\','/') + "/in.html"

  # --user-data-dir required: an already-running Chrome would hijack the command and exit.
  $profileDir = Join-Path $env:TEMP "chrome-headless-render"
  if (Test-Path $profileDir) { Remove-Item $profileDir -Recurse -Force }
  $chromeArgs = @('--headless=new','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files',
    '--force-device-scale-factor=3',"--user-data-dir=$profileDir","--screenshot=$wpng","--window-size=$w,$h",
    '--virtual-time-budget=4000',$wurl)
  Start-Process -FilePath $chrome -ArgumentList $chromeArgs -Wait -NoNewWindow

  if (-not (Test-Path $wpng)) { throw "screenshot failed: $wpng not created" }
  Copy-Item $wpng $png -Force
  Copy-Item $wpng (Join-Path $dir "..\$out") -Force
  Write-Host "  built  ..\$out  (logical ${w}x${h}, 3x output)"
}

Write-Host "[build] using: $chrome"
Render "02" 1520 824 "02_completion_order.png"
Render "03" 1520 960 "03_page_lock.png"
Render "04" 1520 892 "04_one_pending_pattern.png"
Render "05" 1520 846 "05_page_lock_basics.png"
Write-Host "[build] done."
