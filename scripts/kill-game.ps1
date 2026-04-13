param(
    [string]$ProcessName = "GameAdapter",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# Find all matching processes
$Processes = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

if (-not $Processes) {
    Write-Host "No '$ProcessName' processes found" -ForegroundColor Yellow
    exit 0
}

Write-Host "Found $($Processes.Count) process(es):" -ForegroundColor Cyan
$Processes | Format-Table Id, ProcessName, CPU, @{Label="Memory(MB)";Expression={[math]::Round($_.WorkingSet/1MB,2)}} -AutoSize

if ($Force) {
    Write-Host "Killing processes..." -ForegroundColor Red
    $Processes | Stop-Process -Force
    Write-Host "All '$ProcessName' processes terminated" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "Use -Force to kill these processes" -ForegroundColor Yellow
}
