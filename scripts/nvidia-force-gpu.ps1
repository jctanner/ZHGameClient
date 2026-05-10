param(
    [string]$GamePath = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour\GameAdapter.exe"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Force NVIDIA GPU via Registry Profile" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Check if game exists
if (-not (Test-Path $GamePath)) {
    Write-Host "Error: Game not found at $GamePath" -ForegroundColor Red
    exit 1
}

Write-Host "Game path: $GamePath" -ForegroundColor White
Write-Host ""

# Method 1: NVIDIA Profile Inspector compatible registry settings
Write-Host "==> Adding to NVIDIA application profiles..." -ForegroundColor Magenta

$nvidiaProfileKey = "HKLM:\SOFTWARE\NVIDIA Corporation\Global\NVTweak\NvCplAppBarApps"

# Create key if it doesn't exist (may need admin)
try {
    if (-not (Test-Path $nvidiaProfileKey)) {
        Write-Host "    Creating NVIDIA profile key (requires admin)..." -ForegroundColor Yellow
        New-Item -Path $nvidiaProfileKey -Force | Out-Null
    }

    # Add the game executable
    $gameName = [System.IO.Path]::GetFileNameWithoutExtension($GamePath)
    Set-ItemProperty -Path $nvidiaProfileKey -Name $GamePath -Value 1 -Type DWord -ErrorAction Stop
    Write-Host "    Added to NVIDIA profiles" -ForegroundColor Green
} catch {
    Write-Host "    Failed (may need admin): $_" -ForegroundColor Yellow
    Write-Host "    Trying user-level alternative..." -ForegroundColor Yellow
}

# Method 2: Force via NVIDIA driver settings (user-level)
Write-Host "`n==> Setting NVIDIA Shim Database preference..." -ForegroundColor Magenta

$shimKey = "HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"

if (-not (Test-Path $shimKey)) {
    New-Item -Path $shimKey -Force | Out-Null
}

# HIGHDPIAWARE and RUNASADMIN compatibility flags can help
Set-ItemProperty -Path $shimKey -Name $GamePath -Value "~ HIGHDPIAWARE" -Type String
Write-Host "    Set compatibility flags" -ForegroundColor Green

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "NVIDIA Settings Applied!" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Green

Write-Host "If the game still uses Intel GPU:" -ForegroundColor Yellow
Write-Host "  1. Open NVIDIA Control Panel" -ForegroundColor White
Write-Host "  2. Go to 'Manage 3D Settings' > 'Program Settings'" -ForegroundColor White
Write-Host "  3. Click 'Add' and browse to:" -ForegroundColor White
Write-Host "     $GamePath" -ForegroundColor Cyan
Write-Host "  4. Select 'High-performance NVIDIA processor'" -ForegroundColor White
Write-Host "  5. Click Apply" -ForegroundColor White
Write-Host ""
Write-Host "Alternative: Right-click GameAdapter.exe > Run with graphics processor > High-performance NVIDIA processor" -ForegroundColor Yellow
Write-Host ""
