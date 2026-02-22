param(
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path,
    [string]$QtRoot = ""
)

$ErrorActionPreference = "Stop"

function Ensure-Dir([string]$Path) {
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

function Ensure-JunctionRemoved([string]$Path) {
    if (-not (Test-Path $Path)) {
        return
    }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        cmd /c rmdir "$Path" | Out-Null
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

if (-not $QtRoot) {
    $qtCandidates = @(
        "C:\Qt\6.10.2\mingw_64",
        "C:\Qt\6.10.1\mingw_64",
        "C:\Qt\6.9.0\mingw_64"
    )
    foreach ($candidate in $qtCandidates) {
        if (Test-Path (Join-Path $candidate "lib\cmake\Qt6\Qt6Config.cmake")) {
            $QtRoot = $candidate
            break
        }
    }
}

if ($QtRoot -and (Test-Path (Join-Path $QtRoot "lib\cmake\Qt6\Qt6Config.cmake"))) {
    Write-Host "[4/4] Syncing local Qt runtime+headers from $QtRoot ..."
    $qtDest = Join-Path $localDeps "qt6"
    Ensure-Dir $qtDest

    Ensure-JunctionRemoved (Join-Path $qtDest "include")
    Ensure-JunctionRemoved (Join-Path $qtDest "mkspecs")
    Ensure-JunctionRemoved (Join-Path $qtDest "plugins")
    Ensure-Dir (Join-Path $qtDest "include")
    Ensure-Dir (Join-Path $qtDest "mkspecs")
    Ensure-Dir (Join-Path $qtDest "plugins")
    Ensure-Dir (Join-Path $qtDest "lib")
    Ensure-Dir (Join-Path $qtDest "bin")

    robocopy (Join-Path $QtRoot "include") (Join-Path $qtDest "include") /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
    robocopy (Join-Path $QtRoot "mkspecs") (Join-Path $qtDest "mkspecs") /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
    robocopy (Join-Path $QtRoot "plugins") (Join-Path $qtDest "plugins") /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
    robocopy (Join-Path $QtRoot "bin") (Join-Path $qtDest "bin") /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
    robocopy (Join-Path $QtRoot "lib") (Join-Path $qtDest "lib") /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
} else {
    Write-Host "[4/4] Qt root not found. Skipping local Qt sync." -ForegroundColor Yellow
    Write-Host "      Pass -QtRoot 'C:\Qt\<version>\mingw_64' to mirror Qt into local-deps/qt6."
}

Write-Host "Done. Reconfigure with:"
Write-Host "  cmake -S . -B build-mingw -DNOX_ENABLE_PROTOBUF_CONTROL=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
