# Rebuild infographic PNGs from HTML sources (folder 11 — 게임스레드 코어 격리 → 기각).
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
# Renders each <name>.html at 1200x700 x DSF3 -> ..\<name>.png (skips missing HTML).
$dir = $PSScriptRoot

$chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" }
if (-not (Test-Path $chrome)) { throw "Chrome/Edge not found" }

# name; w; h  (logical size must equal HTML body width/height). Output = ..\<name>.png
$targets = @(
  @{ n = "01_summary";    w = 1200; h = 700 },
  @{ n = "02_design";     w = 1200; h = 700 },
  @{ n = "03_bottleneck"; w = 1200; h = 700 },
  @{ n = "04_ceiling";    w = 1200; h = 700 },
  @{ n = "05_verdict";    w = 1200; h = 700 }
)

$render = Join-Path $env:TEMP "ig-render-f11"
$profileDir = Join-Path $env:TEMP ("ig-prof-" + [Guid]::NewGuid().ToString('N').Substring(0,8))
if (Test-Path $render) { Remove-Item $render -Recurse -Force }
New-Item -ItemType Directory -Path $render -Force | Out-Null
Copy-Item (Join-Path $dir "*.html") $render -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $dir "fonts")  $render -Recurse -Force

Write-Host "[build] using: $chrome"
foreach ($t in $targets) {
  $html = Join-Path $render ($t.n + ".html")
  if (-not (Test-Path $html)) { Write-Host "  skip   $($t.n) (no html)"; continue }
  $png = Join-Path $render ($t.n + ".png")
  if (Test-Path $png) { Remove-Item $png -Force }
  $url = "file:///" + ($render -replace '\\','/') + "/" + $t.n + ".html"
  $chromeArgs = @(
    '--headless=new','--disable-gpu','--hide-scrollbars','--allow-file-access-from-files',
    "--user-data-dir=$profileDir",'--force-device-scale-factor=3',
    "--screenshot=$png","--window-size=$($t.w),$($t.h)",$url
  )
  Start-Process -FilePath $chrome -ArgumentList $chromeArgs -NoNewWindow -Wait
  if (-not (Test-Path $png)) { throw "render failed: $($t.n)" }
  Copy-Item $png (Join-Path $dir ("..\" + $t.n + ".png")) -Force
  Write-Host "  built  ..\$($t.n).png  (logical $($t.w)x$($t.h), 3x)"
}
Remove-Item $render -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "[build] done."
