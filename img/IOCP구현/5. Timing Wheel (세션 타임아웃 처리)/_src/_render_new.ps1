# Render new infographics 05/06/07 -> PNG at 3x.
# Two workarounds for this environment's headless Chrome:
#  1) UNIQUE --user-data-dir per call, so a running Chrome can't hijack the
#     request ("opening in existing browser session") and skip the screenshot.
#  2) Render with a TALLER window than the card, then crop to the target size.
#     (At a window height close to the body height, headless clips the card ~80px
#      short, cutting off the footer. Extra headroom paints the full card.)
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot
Add-Type -AssemblyName System.Drawing

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe" }
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }
$base = Join-Path $env:TEMP "wheel-render"

function Render($name, $w, $h) {
  $png  = Join-Path $dir "$name.png"
  $t0   = Get-Date
  $prof = Join-Path $base ([guid]::NewGuid().ToString('N'))
  $url  = "file:///" + ($dir -replace '\\','/') + "/$name.html"
  $rh   = $h + 180   # render taller so the card is not clipped
  & $chrome --headless --disable-gpu --hide-scrollbars --allow-file-access-from-files `
    --no-first-run --no-default-browser-check --disable-extensions `
    --force-device-scale-factor=3 "--user-data-dir=$prof" "--screenshot=$png" "--window-size=$w,$rh" $url | Out-Null
  $waited = 0
  while ((-not (Test-Path $png) -or (Get-Item $png).LastWriteTime -lt $t0) -and $waited -lt 15000) {
    Start-Sleep -Milliseconds 200; $waited += 200
  }
  if (-not ((Test-Path $png) -and (Get-Item $png).LastWriteTime -ge $t0)) { Write-Host "  FAILED: $name"; return }
  # crop top-left to target size (w*3 x h*3)
  $tw = $w * 3; $th = $h * 3
  $img = [System.Drawing.Image]::FromFile($png)
  $bmp = New-Object System.Drawing.Bitmap($tw, $th)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.DrawImage($img, (New-Object System.Drawing.Rectangle(0,0,$tw,$th)), (New-Object System.Drawing.Rectangle(0,0,$tw,$th)), [System.Drawing.GraphicsUnit]::Pixel)
  $g.Dispose(); $img.Dispose()          # release the file lock before overwriting
  $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Write-Host ("  OK  {0}.png  {1}x{2}px  {3:N0} bytes" -f $name, $tw, $th, (Get-Item $png).Length)
}

Write-Host "[render] using: $chrome"
Render "05" 1200 600
Render "06" 1200 640
Render "07" 1200 628

Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -like '*wheel-render*' } |
  ForEach-Object { try { Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop } catch {} }
Write-Host "[render] done."
