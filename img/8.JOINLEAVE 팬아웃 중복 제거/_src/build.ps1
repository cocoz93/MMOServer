# Rebuild infographic PNGs from the HTML sources (PowerShell).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#
# logical size x DSF4 = final resolution (Pretendard embedded, PPT quality):
#   01 : 1200x600 -> 4800x2400   (01_bottleneck_54pct.png)
#   02 : 1200x700 -> 4800x2800   (02_two_directions.png)
#   03 : 1200x660 -> 4800x2640   (03_p1_outbound_fanout.png)
#   04 : 1200x640 -> 4800x2560   (04_p2_inbound_bundle.png)
#   05 : 1200x660 -> 4800x2640   (05_result_68pct.png)
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

# Render inside an ASCII temp dir (source folder name has spaces + Korean, which breaks
# Start-Process arg splitting and the file:// URL). Fresh per-run GUID profile prevents
# attaching to a running Chrome (screenshot no-op). See project_infographic_render_fix.
$render = Join-Path $env:TEMP "ig8-render"
$profileDir = Join-Path $env:TEMP ("ig8-prof-" + [Guid]::NewGuid().ToString('N').Substring(0,8))
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
  Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -Wait
  if (-not (Test-Path $png)) { throw "render failed: $name (no screenshot produced)" }
  Copy-Item $png (Join-Path $dir "$name.png") -Force        # keep a _src preview copy
  Copy-Item $png (Join-Path $dir "..\$out") -Force          # deliverable in parent folder
  Write-Host "  built  ..\$out  (logical ${w}x${h}, 4x output)"
}

Write-Host "[build] using: $chrome"
Render "01" 1200 600 "01_bottleneck_54pct.png"
Render "02" 1200 700 "02_two_directions.png"
Render "03" 1200 660 "03_p1_outbound_fanout.png"
Render "04" 1200 640 "04_p2_inbound_bundle.png"
Render "05" 1200 660 "05_result_68pct.png"
Remove-Item $render -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "[build] done."
