param(
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [string]$Executable = "GameAdapter.exe",
    [string]$Arguments = "-win"
)

$ErrorActionPreference = "Stop"

$GamePath = Join-Path $GameDir $Executable

# Check if executable exists
if (-not (Test-Path $GamePath)) {
    Write-Error "Game executable not found: $GamePath"
    exit 1
}

Write-Host "=== Session Group Game Launcher ===" -ForegroundColor Cyan
Write-Host ""

# Query session information
Write-Host "Querying Windows sessions..." -ForegroundColor Yellow
$querySession = query session 2>$null
Write-Host $querySession
Write-Host ""

# Find console session
$consoleSessionId = $null
foreach ($line in $querySession -split "`n") {
    if ($line -match 'console' -and $line -match 'Active') {
        if ($line -match '\s+(\d+)\s+Active') {
            $consoleSessionId = $matches[1]
            Write-Host "Found active console session ID: $consoleSessionId" -ForegroundColor Green
            break
        }
    }
}

if (-not $consoleSessionId) {
    Write-Error "Could not find active console session"
    exit 1
}

# Get current user
$CurrentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
Write-Host "Current user: $CurrentUser"
Write-Host "Target session: $consoleSessionId"
Write-Host ""

# Create a unique task name
$TaskName = "GameLauncher_Group_$(Get-Date -Format 'yyyyMMddHHmmss')"

Write-Host "Creating scheduled task: $TaskName" -ForegroundColor Yellow
Write-Host "Executable: $GamePath"
Write-Host "Arguments: $Arguments"
Write-Host ""

# Create action
$Action = New-ScheduledTaskAction -Execute $GamePath -Argument $Arguments -WorkingDirectory $GameDir

# Create trigger (immediate)
$Trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(2)

# Create settings
$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Hours 4)

# Try Group logon type - designed for interactive session
$Principal = New-ScheduledTaskPrincipal `
    -GroupId "BUILTIN\Users" `
    -RunLevel Highest

Write-Host "Task configuration:" -ForegroundColor Cyan
Write-Host "  Group: BUILTIN\Users"
Write-Host "  Logon Type: Group (interactive session)"
Write-Host "  Run Level: Highest"
Write-Host ""

# Register the task
try {
    $Task = Register-ScheduledTask `
        -TaskName $TaskName `
        -Action $Action `
        -Trigger $Trigger `
        -Settings $Settings `
        -Principal $Principal `
        -Force

    Write-Host "Task registered successfully!" -ForegroundColor Green
} catch {
    Write-Error "Failed to register task: $_"
    exit 1
}

# Start the task immediately
Write-Host "Starting task now..." -ForegroundColor Yellow
try {
    Start-ScheduledTask -TaskName $TaskName
    Write-Host "Task started!" -ForegroundColor Green
} catch {
    Write-Warning "Task start failed: $_"
}

# Wait for process to start
Write-Host ""
Write-Host "Waiting for game process (7 seconds)..." -ForegroundColor Yellow
Start-Sleep -Seconds 7

# Check if process is running
$ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
$RunningProcess = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

if ($RunningProcess) {
    Write-Host ""
    Write-Host "=== Game Launched ===" -ForegroundColor Green
    Write-Host "Process ID: $($RunningProcess.Id)"
    Write-Host "Memory: $([math]::Round($RunningProcess.WorkingSet/1MB, 2)) MB"
    Write-Host "Window Handle: $($RunningProcess.MainWindowHandle)"

    # Check session
    try {
        $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($RunningProcess.Id)"
        $procSessionId = $proc.SessionId
        Write-Host "Process Session ID: $procSessionId"

        if ($procSessionId -eq $consoleSessionId) {
            Write-Host "[OK] In console session $consoleSessionId" -ForegroundColor Green
        } elseif ($procSessionId -eq 0) {
            Write-Warning "[!] In Session 0 (non-interactive) - GROUP LOGON FAILED"
        } else {
            Write-Warning "[!] In session $procSessionId, expected $consoleSessionId"
        }
    } catch {
        Write-Host "Session check: $_" -ForegroundColor Gray
    }

    if ($RunningProcess.MainWindowHandle -ne 0) {
        Write-Host "Window: VISIBLE" -ForegroundColor Green
    } else {
        Write-Warning "Window: HIDDEN (handle = 0)"
    }
} else {
    Write-Warning "Process not found"

    # Check task result
    $TaskInfo = Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($TaskInfo) {
        Write-Host "Task Last Result: $($TaskInfo.LastTaskResult)" -ForegroundColor Yellow
        if ($TaskInfo.LastTaskResult -ne 0) {
            Write-Host "Error code: 0x$([Convert]::ToString($TaskInfo.LastTaskResult, 16).ToUpper())"
        }
    }
}

# Clean up
Write-Host ""
Write-Host "Cleaning up..." -ForegroundColor Yellow
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
Write-Host "Done!" -ForegroundColor Green
