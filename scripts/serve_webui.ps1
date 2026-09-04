# Serve the patch admin UI for local iteration (demo mocks, no ESP).
# Open http://127.0.0.1:8093/index.html
# 8090 = px-wifi-v1, 8091 = px-fuse-v1, 8092 = px-valve-v1.
$ErrorActionPreference = "Stop"
$port = 8093
$webui = Join-Path $PSScriptRoot "..\main\webui"
Set-Location $webui
Write-Host "Serving $((Resolve-Path $webui).Path) on http://127.0.0.1:$port/"
Write-Host "Demo mocks auto-enable on localhost. Ctrl+C to stop."
python -m http.server $port --bind 127.0.0.1
