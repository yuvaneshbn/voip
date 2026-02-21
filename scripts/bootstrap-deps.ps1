param(
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
)

$ErrorActionPreference = "Stop"

function Ensure-Dir([string]$Path) {
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

$localDeps = Join-Path $RepoRoot "local-deps"
$downloads = Join-Path $localDeps "downloads"
Ensure-Dir $localDeps
Ensure-Dir $downloads

Write-Host "[1/3] Downloading Asio..."
$asioZip = Join-Path $downloads "asio-1.30.2.zip"
if (-not (Test-Path $asioZip)) {
    Invoke-WebRequest -Uri "https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.zip" -OutFile $asioZip
}
Expand-Archive -Path $asioZip -DestinationPath $downloads -Force
Ensure-Dir (Join-Path $localDeps "asio")
Copy-Item -Recurse -Force (Join-Path $downloads "asio-asio-1-30-2\asio\include") (Join-Path $localDeps "asio\include")

Write-Host "[2/3] Downloading SpeexDSP source..."
$speexZip = Join-Path $downloads "speexdsp-1.2.1.zip"
if (-not (Test-Path $speexZip)) {
    Invoke-WebRequest -Uri "https://github.com/xiph/speexdsp/archive/refs/tags/SpeexDSP-1.2.1.zip" -OutFile $speexZip
}
Expand-Archive -Path $speexZip -DestinationPath $downloads -Force
Ensure-Dir (Join-Path $localDeps "speexdsp\source")
Copy-Item -Recurse -Force (Join-Path $downloads "speexdsp-SpeexDSP-1.2.1\*") (Join-Path $localDeps "speexdsp\source")

Write-Host "[3/3] Checking protobuf tools..."
$protoc = "C:\msys64\ucrt64\bin\protoc.exe"
if (Test-Path $protoc) {
    Write-Host "Found protoc: $protoc"
} else {
    Write-Host "protoc not found at $protoc (protobuf control remains optional)." -ForegroundColor Yellow
}

Write-Host "Done. Reconfigure with:"
Write-Host "  cmake -S . -B build-mingw -DNOX_ENABLE_PROTOBUF_CONTROL=OFF"
