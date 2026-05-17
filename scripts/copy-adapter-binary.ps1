param(
    [string]$BuildDir = "build/win32",
    [string]$Config = "Release",
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [switch]$Backup
)

$ErrorActionPreference = "Stop"

# Resolve source executable from common CMake output layouts
$SourceCandidates = @(
    (Join-Path $BuildDir "GeneralsMD\$Config\GeneralsOnlineZH.exe"),
    (Join-Path $BuildDir "GeneralsMD\GeneralsOnlineZH.exe"),
    (Join-Path $BuildDir "$Config\GeneralsOnlineZH.exe"),
    (Join-Path $BuildDir "GeneralsOnlineZH.exe")
)

$SourceExe = $null
foreach ($Candidate in $SourceCandidates) {
    if (Test-Path $Candidate) {
        $SourceExe = $Candidate
        break
    }
}

$ResolvedBySearch = $false
if (-not $SourceExe) {
    $Found = Get-ChildItem -Path $BuildDir -Filter GeneralsOnlineZH.exe -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($Found) {
        $SourceExe = $Found.FullName
        $ResolvedBySearch = $true
    }
}

$DestExe = Join-Path $GameDir "GameAdapter.exe"

# Check if source exists
if (-not $SourceExe -or -not (Test-Path $SourceExe)) {
    Write-Host "Checked source candidates:"
    $SourceCandidates | ForEach-Object { Write-Host "  $_" }
    Write-Error "Source executable not found under build dir: $BuildDir"
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
if ($ResolvedBySearch) {
    Write-Host "  Resolved via recursive search under build dir"
}

Copy-Item $SourceExe $DestExe -Force

# Verify hashes match after copy
$SourceHash = (Get-FileHash $SourceExe -Algorithm SHA256).Hash
$DestHash = (Get-FileHash $DestExe -Algorithm SHA256).Hash

Write-Host ""
Write-Host "SHA256:"
Write-Host "  Source: $SourceHash"
Write-Host "  Dest:   $DestHash"

if ($SourceHash -ne $DestHash) {
    Write-Error "Hash mismatch after copy. Source and destination binaries differ."
    exit 1
}

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
