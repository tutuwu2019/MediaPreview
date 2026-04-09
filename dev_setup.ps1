param(
    [string]$QtRoot = "D:/QT/6.9.3/mingw_64",
    [string]$MingwRoot = "D:/QT/Tools/mingw1310_64",
    [ValidateSet("auto", "msvc", "mingw")]
    [string]$Toolchain = "auto",
    [ValidateSet("debug", "release")]
    [string]$Config = "debug"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $QtRoot)) {
    throw "QtRoot not found: $QtRoot"
}

$env:QT_ROOT = $QtRoot
$env:PATH = "$env:QT_ROOT/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$env:QT_ROOT/plugins"

if ($Toolchain -eq "auto") {
    if ($QtRoot -match "mingw") {
        $Toolchain = "mingw"
    } elseif ($QtRoot -match "msvc") {
        $Toolchain = "msvc"
    } else {
        $Toolchain = "mingw"
    }
}

if ($Toolchain -eq "mingw") {
    if (-not (Test-Path "$MingwRoot/bin/g++.exe")) {
        throw "MinGW compiler not found under: $MingwRoot"
    }
    $env:MINGW_ROOT = $MingwRoot
    $env:PATH = "$env:MINGW_ROOT/bin;$env:PATH"
}

Write-Host "QT_ROOT=$env:QT_ROOT"
Write-Host "QT_PLUGIN_PATH=$env:QT_PLUGIN_PATH"
if ($Toolchain -eq "mingw") {
    Write-Host "MINGW_ROOT=$env:MINGW_ROOT"
}

$preset = ""
$buildPreset = ""

if ($Toolchain -eq "msvc") {
    if ($Config -eq "debug") {
        $preset = "msvc-debug"
        $buildPreset = "build-msvc-debug"
    } else {
        $preset = "msvc-release"
        $buildPreset = "build-msvc-release"
    }
} else {
    if ($Config -eq "debug") {
        $preset = "mingw-debug"
        $buildPreset = "build-mingw-debug"
    } else {
        $preset = "mingw-release"
        $buildPreset = "build-mingw-release"
    }
}

cmake --preset $preset
cmake --build --preset $buildPreset

Write-Host "Setup done. You can now launch from VS Code launch configs."
