param(
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [string]$Executable = "GeneralsOnlineZH.exe",
    [string]$Arguments = "-win",
    [string]$TaskName = "TempGameLauncher",
    [int]$DelaySeconds = 2
)

$ErrorActionPreference = "Stop"

$GamePath = Join-Path $GameDir $Executable

# Check if executable exists
if (-not (Test-Path $GamePath)) {
    Write-Error "Game executable not found: $GamePath"
    exit 1
}

# Calculate start time (current time + delay)
$StartTime = (Get-Date).AddSeconds($DelaySeconds).ToString("HH:mm:ss")
$StartDate = (Get-Date).ToString("MM/dd/yyyy")

Write-Host "Creating scheduled task to launch game in user session..."
Write-Host "Executable: $Executable"
Write-Host "Arguments: $Arguments"
Write-Host "Scheduled for: $StartDate $StartTime (in $DelaySeconds seconds)"
Write-Host ""

# Create a temporary VBS script to launch the game (handles paths with special chars)
$TempVBS = Join-Path $env:TEMP "launch_game_temp.vbs"
$VBSContent = @"
Set WshShell = CreateObject("WScript.Shell")
WshShell.CurrentDirectory = "$($GameDir -replace '\\','\\')"
WshShell.Run """$($GamePath -replace '\\','\\')""" & " $Arguments", 1, False
"@
Set-Content -Path $TempVBS -Value $VBSContent

# Delete existing task if it exists (suppress errors if it doesn't exist)
try {
    schtasks /delete /tn "$TaskName" /f 2>&1 | Out-Null
} catch {
    # Task doesn't exist, that's fine
}

# Create scheduled task with VBS script
# /RL HIGHEST runs with highest privileges
# /SC ONCE runs once at specified time
# /ST specifies start time
# /SD specifies start date
# /F forces creation without prompting
$CreateResult = schtasks /create /tn "$TaskName" /tr "wscript.exe `"$TempVBS`"" /sc once /st $StartTime /sd $StartDate /rl highest /f

if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to create scheduled task"
    exit 1
}

Write-Host "Task created successfully!" -ForegroundColor Green
Write-Host "Task will execute in $DelaySeconds seconds..."
Write-Host ""

# Wait for the scheduled time + 1 extra second
Start-Sleep -Seconds ($DelaySeconds + 1)

# Check if task ran successfully
$TaskInfo = schtasks /query /tn "$TaskName" /fo csv /nh 2>$null
if ($TaskInfo) {
    # Parse the status (4th field in CSV)
    $Status = ($TaskInfo -split ',')[3] -replace '"', ''

    Write-Host "Task Status: $Status"

    # Check if process is running
    $ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
    $RunningProcess = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

    if ($RunningProcess) {
        Write-Host ""
        Write-Host "Game launched successfully!" -ForegroundColor Green
        Write-Host "Process ID: $($RunningProcess.Id)"
        Write-Host "Process Name: $($RunningProcess.ProcessName)"
        Write-Host "Memory: $([math]::Round($RunningProcess.WorkingSet/1MB, 2)) MB"
    } else {
        Write-Warning "Process not found - game may have exited or failed to start"
    }
}

# Clean up the scheduled task
Write-Host ""
Write-Host "Cleaning up scheduled task..."
schtasks /delete /tn "$TaskName" /f 2>$null | Out-Null
Write-Host "Done!" -ForegroundColor Green
