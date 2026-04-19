param(
    [string]$Tag = "v1.1.0",
    [string]$QtRoot = "D:/QT/6.9.3/mingw_64",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

Require-Command git
Require-Command gh

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not $SkipBuild) {
    cmake --preset mingw-release
    cmake --build --preset build-mingw-release
    .\deploy.ps1 -QtRoot $QtRoot -Toolchain mingw -Config Release
}

$assetZip = "MediaPreviewClient-$Tag-windows-x64.zip"
if (Test-Path $assetZip) {
    Remove-Item $assetZip -Force
}

Compress-Archive -Path "dist/mingw-release/*" -DestinationPath $assetZip -Force
Get-FileHash $assetZip -Algorithm SHA256 | ForEach-Object {
    "{0}  {1}" -f $_.Hash.ToLower(), $assetZip
} | Set-Content "SHA256SUMS.txt" -Encoding UTF8

if ((git tag -l $Tag) -eq "") {
    git tag -a $Tag -m $Tag
}

git push origin main
git push origin $Tag

$releaseExists = gh release view $Tag 2>$null
if ($LASTEXITCODE -eq 0 -and $releaseExists) {
    gh release upload $Tag $assetZip SHA256SUMS.txt scripts/install-windows.ps1 --clobber
} else {
    gh release create $Tag $assetZip SHA256SUMS.txt scripts/install-windows.ps1 --generate-notes --title $Tag
}

Write-Host "Release published: $Tag" -ForegroundColor Green
