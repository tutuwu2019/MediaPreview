param(
    [string]$Owner = "tutuwu2019",
    [string]$Repo = "MediaPreview",
    [string]$InstallDir = "$env:LOCALAPPDATA\MediaPreviewClient",
    [switch]$NoDesktopShortcut,
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"

Write-Host "Fetching latest release metadata from GitHub..." -ForegroundColor Cyan
$releaseApi = "https://api.github.com/repos/$Owner/$Repo/releases/latest"
$release = Invoke-RestMethod -Uri $releaseApi -Headers @{"User-Agent" = "MediaPreviewClient-Installer"}

$zipAsset = $release.assets | Where-Object { $_.name -match '^MediaPreviewClient-.*-windows-x64\.zip$' } | Select-Object -First 1
if (-not $zipAsset) {
    throw "No release asset matching MediaPreviewClient-*-windows-x64.zip was found."
}

if (Test-Path $InstallDir) {
    Write-Host "Removing existing installation: $InstallDir" -ForegroundColor Yellow
    Remove-Item $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

$tmpZip = Join-Path $env:TEMP $zipAsset.name
if (Test-Path $tmpZip) {
    Remove-Item $tmpZip -Force
}

Write-Host "Downloading $($zipAsset.name)..." -ForegroundColor Cyan
Invoke-WebRequest -Uri $zipAsset.browser_download_url -OutFile $tmpZip

Write-Host "Extracting to $InstallDir ..." -ForegroundColor Cyan
Expand-Archive -Path $tmpZip -DestinationPath $InstallDir -Force

$exePath = Join-Path $InstallDir "MediaPreviewClient.exe"
if (-not (Test-Path $exePath)) {
    throw "Installation failed: MediaPreviewClient.exe not found in $InstallDir"
}

if (-not $NoDesktopShortcut) {
    try {
        $desktop = [Environment]::GetFolderPath("Desktop")
        $shortcutPath = Join-Path $desktop "MediaPreviewClient.lnk"
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $exePath
        $shortcut.WorkingDirectory = $InstallDir
        $shortcut.IconLocation = "$exePath,0"
        $shortcut.Save()
        Write-Host "Desktop shortcut created: $shortcutPath" -ForegroundColor Green
    } catch {
        Write-Warning "Failed to create desktop shortcut: $($_.Exception.Message)"
    }
}

Write-Host "Installation completed: $InstallDir" -ForegroundColor Green

if (-not $NoLaunch) {
    Start-Process -FilePath $exePath
}
