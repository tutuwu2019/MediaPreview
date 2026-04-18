param(
    [string]$QtRoot = "D:/QT/6.9.3/mingw_64",
    [ValidateSet("mingw", "msvc")]
    [string]$Toolchain = "mingw",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$BuildDir,
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

function Test-DeploymentPackage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeployDir
    )

    $requiredFiles = @(
        "MediaPreviewClient.exe",
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Multimedia.dll",
        "Qt6MultimediaWidgets.dll",
        "platforms/qwindows.dll",
        "imageformats/qgif.dll"
    )

    $requiredDirs = @(
        "imageformats",
        "multimedia",
        "platforms"
    )

    $missing = @()

    foreach ($file in $requiredFiles) {
        $path = Join-Path $DeployDir $file
        if (-not (Test-Path $path -PathType Leaf)) {
            $missing += $file
        }
    }

    foreach ($dir in $requiredDirs) {
        $path = Join-Path $DeployDir $dir
        if (-not (Test-Path $path -PathType Container)) {
            $missing += "$dir/"
        }
    }

    $imageFormatsPattern = Join-Path (Join-Path $DeployDir "imageformats") "*.dll"
    $multimediaPattern = Join-Path (Join-Path $DeployDir "multimedia") "*.dll"
    $hasImageFormatsDll = @(Get-ChildItem -Path $imageFormatsPattern -ErrorAction SilentlyContinue).Count -gt 0
    $hasMultimediaDll = @(Get-ChildItem -Path $multimediaPattern -ErrorAction SilentlyContinue).Count -gt 0

    if (-not $hasImageFormatsDll) {
        $missing += "imageformats/*.dll"
    }
    if (-not $hasMultimediaDll) {
        $missing += "multimedia/*.dll"
    }

    if ($missing.Count -gt 0) {
        Write-Host ""
        Write-Host "Package check failed. Missing items:" -ForegroundColor Red
        foreach ($item in $missing) {
            Write-Host "  - $item" -ForegroundColor Red
        }
        throw "Deployment output is incomplete."
    }

    Write-Host "Package check passed." -ForegroundColor Green
}

function Ensure-GifPlugin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DeployDir,
        [Parameter(Mandatory = $true)]
        [string]$QtBaseDir
    )

    $targetGif = Join-Path $DeployDir "imageformats/qgif.dll"
    if (Test-Path $targetGif -PathType Leaf) {
        return
    }

    $sourceGif = Join-Path $QtBaseDir "plugins/imageformats/qgif.dll"
    if (-not (Test-Path $sourceGif -PathType Leaf)) {
        throw "qgif.dll not found in Qt installation: $sourceGif"
    }

    $targetDir = Join-Path $DeployDir "imageformats"
    if (-not (Test-Path $targetDir -PathType Container)) {
        New-Item -ItemType Directory -Path $targetDir | Out-Null
    }

    Copy-Item -Path $sourceGif -Destination $targetGif -Force
    Write-Host "qgif.dll was missing and has been copied manually." -ForegroundColor Yellow
}

if (-not (Test-Path $QtRoot)) {
    throw "QtRoot not found: $QtRoot"
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $suffix = if ($Toolchain -eq "mingw") { "mingw" } else { "msvc" }
    $BuildDir = Join-Path $PSScriptRoot "build/$suffix-$($Config.ToLower())"
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $PSScriptRoot "dist/$Toolchain-$($Config.ToLower())"
}

$exePath = Join-Path $BuildDir "MediaPreviewClient.exe"
if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath. Please build first."
}

$qtBin = Join-Path $QtRoot "bin"
$windeployqt = Join-Path $qtBin "windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found: $windeployqt"
}

Write-Host "QtRoot: $QtRoot"
Write-Host "Toolchain: $Toolchain"
Write-Host "Config: $Config"
Write-Host "BuildDir: $BuildDir"
Write-Host "OutputDir: $OutputDir"

if (Test-Path $OutputDir) {
    try {
        Remove-Item -Path $OutputDir -Recurse -Force
    } catch {
        $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $fallbackDir = "$OutputDir-$timestamp"
        Write-Host "OutputDir is locked, fallback to: $fallbackDir" -ForegroundColor Yellow
        $OutputDir = $fallbackDir
    }
}
New-Item -ItemType Directory -Path $OutputDir | Out-Null

Copy-Item -Path $exePath -Destination (Join-Path $OutputDir "MediaPreviewClient.exe") -Force

$deployArgs = @(
    "--force",
    "--verbose", "1",
    "--dir", $OutputDir,
    "--qmldir", $PSScriptRoot,
    "--no-translations",
    "--no-system-d3d-compiler",
    "--no-opengl-sw",
    (Join-Path $OutputDir "MediaPreviewClient.exe")
)

$preferDebugDeploy = ($Config -eq "Debug")
if ($preferDebugDeploy) {
    $deployArgs = @("--debug") + $deployArgs
} else {
    $deployArgs = @("--release") + $deployArgs
}

if ($Toolchain -eq "mingw") {
    $deployArgs = @("--compiler-runtime") + $deployArgs
}

& $windeployqt @deployArgs
if ($LASTEXITCODE -ne 0) {
    if ($preferDebugDeploy) {
        Write-Host "windeployqt --debug failed, retrying with --release plugins for debug runtime..." -ForegroundColor Yellow
        $retryArgs = $deployArgs | Where-Object { $_ -ne "--debug" }
        $retryArgs = @("--release") + $retryArgs
        & $windeployqt @retryArgs
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt fallback failed with exit code $LASTEXITCODE"
        }
    } else {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
}

Ensure-GifPlugin -DeployDir $OutputDir -QtBaseDir $QtRoot

Test-DeploymentPackage -DeployDir $OutputDir

Write-Host ""
Write-Host "Deployment finished: $OutputDir"
Write-Host "Run this EXE on target machine: $(Join-Path $OutputDir 'MediaPreviewClient.exe')"
Write-Host "Note: build output EXE is not a deploy package. Do not run or distribute build/*/MediaPreviewClient.exe." -ForegroundColor Yellow
