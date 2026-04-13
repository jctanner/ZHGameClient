param(
    [string]$BuildDir = "build/win32",
    [string]$Config = "Release",
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [switch]$Backup
)

$ErrorActionPreference = "Stop"

# Source and destination paths
$SourceExe = Join-Path $BuildDir "GeneralsMD\$Config\GeneralsOnlineZH.exe"
$DestExe = Join-Path $GameDir "GameAdapter.exe"

# Check if source exists
if (-not (Test-Path $SourceExe)) {
    Write-Error "Source executable not found: $SourceExe"
    Write-Host "Build the project first with: .\scripts\build-game.ps1 -Target zh"
    exit 1
}

# Check if game directory exists
if (-not (Test-Path $GameDir)) {
    Write-Error "Game directory not found: $GameDir"
    Write-Host "Update the -GameDir parameter to point to your game installation"
    exit 1
}

# Backup existing file if requested
if ($Backup -and (Test-Path $DestExe)) {
    $BackupPath = "$DestExe.backup"
    Write-Host "Creating backup: $BackupPath"
    Copy-Item $DestExe $BackupPath -Force
}

# Copy the file
Write-Host "Copying:"
Write-Host "  From: $SourceExe"
Write-Host "  To:   $DestExe"

Copy-Item $SourceExe $DestExe -Force

# Verify copy
if (Test-Path $DestExe) {
    $SourceSize = (Get-Item $SourceExe).Length
    $DestSize = (Get-Item $DestExe).Length

    if ($SourceSize -eq $DestSize) {
        Write-Host ""
        Write-Host "Successfully copied GameAdapter.exe ($([math]::Round($DestSize/1MB, 2)) MB)" -ForegroundColor Green
        Write-Host "Location: $DestExe"
    } else {
        Write-Warning "File sizes don't match! Source: $SourceSize, Dest: $DestSize"
    }
} else {
    Write-Error "Copy failed - destination file not found"
    exit 1
}
