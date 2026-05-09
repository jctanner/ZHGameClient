param(
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [string]$Executable = "GeneralsOnlineZH.exe",
    [string]$Arguments = "-win"
)

$ErrorActionPreference = "Stop"

$GamePath = Join-Path $GameDir $Executable

# Check if executable exists
if (-not (Test-Path $GamePath)) {
    Write-Error "Game executable not found: $GamePath"
    exit 1
}

Write-Host "Launching game in interactive user session..."
Write-Host "Executable: $Executable"
Write-Host "Arguments: $Arguments"
Write-Host ""

# Use Register-ScheduledTask for better control
$TaskName = "TempGameLauncher_$(Get-Date -Format 'yyyyMMdd_HHmmss')"

# Create action
$Action = New-ScheduledTaskAction -Execute $GamePath -Argument $Arguments -WorkingDirectory $GameDir

# Create trigger (10 seconds from now to avoid timing issues)
$Trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(10)

# Create settings
$Settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable

# Create principal to run as current user with highest privileges
$CurrentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$Principal = New-ScheduledTaskPrincipal -UserId $CurrentUser -LogonType Interactive -RunLevel Highest

# Register the task
$Task = Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings -Principal $Principal -Force

Write-Host "Task registered: $TaskName"
Write-Host "Waiting for execution (10 seconds)..."

# Wait for task to run
Start-Sleep -Seconds 12

# Check if process is running
$ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
$RunningProcess = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

if ($RunningProcess) {
    Write-Host ""
    Write-Host "Game launched successfully!" -ForegroundColor Green
    Write-Host "Process ID: $($RunningProcess.Id)"
    Write-Host "Process Name: $($RunningProcess.ProcessName)"
    Write-Host "Memory: $([math]::Round($RunningProcess.WorkingSet/1MB, 2)) MB"
    Write-Host "Window Handle: $($RunningProcess.MainWindowHandle)"
} else {
    Write-Warning "Process not found - game may have exited immediately"
}

# Clean up task
Write-Host ""
Write-Host "Cleaning up task..."
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
Write-Host "Done!" -ForegroundColor Green
