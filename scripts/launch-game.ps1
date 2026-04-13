param(
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [string]$Executable = "GameAdapter.exe",
    [string]$Arguments = "-win",
    [switch]$NoWindow,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

$GamePath = Join-Path $GameDir $Executable

# Check if executable exists
if (-not (Test-Path $GamePath)) {
    Write-Error "Game executable not found: $GamePath"
    exit 1
}

Write-Host "Launching: $Executable"
Write-Host "Directory: $GameDir"
Write-Host "Arguments: $Arguments"
Write-Host ""

# Launch parameters
$StartParams = @{
    FilePath = $GamePath
    WorkingDirectory = $GameDir
}

if ($Arguments) {
    $StartParams.ArgumentList = $Arguments
}

if ($NoWindow) {
    $StartParams.WindowStyle = 'Hidden'
}

if ($Wait) {
    $StartParams.Wait = $true
}

# Launch the game
$Process = Start-Process @StartParams -PassThru

if ($Process) {
    Write-Host "Game launched successfully!" -ForegroundColor Green
    Write-Host "Process ID: $($Process.Id)"
    Write-Host "Process Name: $($Process.ProcessName)"

    # Wait a moment and check if still running
    Start-Sleep -Seconds 2
    if (Get-Process -Id $Process.Id -ErrorAction SilentlyContinue) {
        Write-Host "Status: Running" -ForegroundColor Green
    } else {
        Write-Warning "Process exited immediately - check for errors"
    }
} else {
    Write-Error "Failed to launch game"
}
