param(
    [string]$GamePath = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour\GameAdapter.exe"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Set GPU Preference for GameAdapter" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Check if game exists
if (-not (Test-Path $GamePath)) {
    Write-Host "Error: Game not found at $GamePath" -ForegroundColor Red
    exit 1
}

Write-Host "Game path: $GamePath" -ForegroundColor White
Write-Host ""

# Method 1: Add to Windows Graphics Settings (requires Windows 10 1803+)
Write-Host "==> Adding to Windows Graphics Settings..." -ForegroundColor Magenta

# Create registry key for Graphics Preference
$graphicsKey = "HKCU:\Software\Microsoft\DirectX\UserGpuPreferences"

if (-not (Test-Path $graphicsKey)) {
    New-Item -Path $graphicsKey -Force | Out-Null
}

# Set to GpuPreference=2 (High Performance = NVIDIA on Optimus)
# GpuPreference values: 0=Auto, 1=Power Saving (Intel), 2=High Performance (NVIDIA)
Set-ItemProperty -Path $graphicsKey -Name $GamePath -Value "GpuPreference=2;" -Type String

Write-Host "    Set GpuPreference=2 (High Performance)" -ForegroundColor Green
Write-Host ""

# Method 2: Set environment variable for NVIDIA Optimus
Write-Host "==> Setting NVIDIA Optimus environment hint..." -ForegroundColor Magenta
Write-Host "    Note: This requires restarting the game" -ForegroundColor Yellow
Write-Host ""

# Create a wrapper script that sets SHIM_RENDERING_MODE
$wrapperPath = Join-Path (Split-Path $GamePath) "LaunchWithNVIDIA.bat"
$wrapperContent = @"
@echo off
REM Force NVIDIA GPU on Optimus systems
set SHIM_RENDERING_MODE=0
set SHIM_MCCOMPAT=0x800000001
start "" "$GamePath" %*
"@

Set-Content -Path $wrapperPath -Value $wrapperContent -Encoding ASCII
Write-Host "    Created wrapper: $wrapperPath" -ForegroundColor Green
Write-Host ""

Write-Host "========================================" -ForegroundColor Green
Write-Host "GPU Preference Set!" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Green

Write-Host "Changes applied:" -ForegroundColor Cyan
Write-Host "  1. Added to Windows Graphics Settings (High Performance)" -ForegroundColor White
Write-Host "  2. Created NVIDIA Optimus wrapper script" -ForegroundColor White
Write-Host ""
Write-Host "IMPORTANT:" -ForegroundColor Yellow
Write-Host "  - Restart the game for changes to take effect" -ForegroundColor White
Write-Host "  - Verify with: nvidia-smi (should show GameAdapter.exe)" -ForegroundColor White
Write-Host ""
