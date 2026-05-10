$ErrorActionPreference = "Continue"

$logPath = "D:\logs\adapter.log"

if (Test-Path $logPath) {
    Write-Host "=== Last 150 lines of adapter log ===" -ForegroundColor Cyan
    Get-Content $logPath -Tail 150
} else {
    Write-Host "Log file not found: $logPath" -ForegroundColor Red
}
