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

Write-Host "=== Console Session Game Launcher ===" -ForegroundColor Cyan
Write-Host ""

# Query session information
Write-Host "Querying Windows sessions..." -ForegroundColor Yellow
$sessions = quser 2>$null
if (-not $sessions) {
    Write-Error "No user sessions found. Is someone logged in to the console?"
    exit 1
}

Write-Host "Active sessions:"
Write-Host $sessions
Write-Host ""

# Parse quser output to find console session
# Format: USERNAME  SESSIONNAME  ID  STATE   IDLE TIME  LOGON TIME
$sessionLines = $sessions -split "`n" | Select-Object -Skip 1
$consoleSession = $null
$consoleSessionId = $null
$consoleUser = $null

foreach ($line in $sessionLines) {
    if ($line -match '\s+console\s+') {
        # Parse the line - handle varying whitespace
        $parts = $line -split '\s+' | Where-Object { $_ -ne '' }
        $consoleUser = $parts[0]
        $consoleSessionId = $parts[2]
        $consoleSession = $line
        break
    }
}

if (-not $consoleSessionId) {
    Write-Warning "No console session found. Trying alternate method..."

    # Alternative: query session and look for 'Active' state
    $querySession = query session 2>$null
    Write-Host $querySession
    Write-Host ""

    foreach ($line in $querySession -split "`n") {
        if ($line -match 'Active' -and $line -match 'console') {
            # Extract session ID (usually 3rd column)
            if ($line -match '\s+(\d+)\s+Active') {
                $consoleSessionId = $matches[1]
                Write-Host "Found active console session ID: $consoleSessionId" -ForegroundColor Green
                break
            }
        }
    }
}

if (-not $consoleSessionId) {
    Write-Error "Could not determine console session ID. Manual intervention required."
    exit 1
}

Write-Host "Target session details:" -ForegroundColor Green
Write-Host "  Session ID: $consoleSessionId"
Write-Host "  User: $consoleUser"
Write-Host "  Type: Console (physical desktop)"
Write-Host ""

# Get current user for scheduled task
$CurrentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
Write-Host "Current executing user: $CurrentUser"
Write-Host "Target user for game: $consoleUser"
Write-Host ""

# Create a unique task name
$TaskName = "GameLauncher_Session${consoleSessionId}_$(Get-Date -Format 'yyyyMMddHHmmss')"

Write-Host "Creating scheduled task: $TaskName" -ForegroundColor Yellow
Write-Host "Executable: $GamePath"
Write-Host "Arguments: $Arguments"
Write-Host "Working Directory: $GameDir"
Write-Host ""

# Create action
$Action = New-ScheduledTaskAction -Execute $GamePath -Argument $Arguments -WorkingDirectory $GameDir

# Create trigger (5 seconds from now)
$Trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(5)

# Create settings - critical settings for interactive GUI
$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Hours 4)

# CRITICAL: Use S4U (Service-for-User) logon type to run in the user's session
# This ensures the task runs in the actual logged-in session, not Session 0
$Principal = New-ScheduledTaskPrincipal `
    -UserId $consoleUser `
    -LogonType S4U `
    -RunLevel Highest

Write-Host "Task configuration:" -ForegroundColor Cyan
Write-Host "  Principal User: $consoleUser"
Write-Host "  Logon Type: S4U (runs in user's interactive session)"
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

# Start the task immediately (overrides the 5-second trigger for faster execution)
Write-Host "Starting task now..." -ForegroundColor Yellow
try {
    Start-ScheduledTask -TaskName $TaskName
    Write-Host "Task started!" -ForegroundColor Green
} catch {
    Write-Warning "Task start failed: $_"
    Write-Host "Task will execute via trigger in 5 seconds..."
}

# Wait for process to start
Write-Host ""
Write-Host "Waiting for game process to start (7 seconds)..." -ForegroundColor Yellow
Start-Sleep -Seconds 7

# Check if process is running
$ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
$RunningProcess = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

if ($RunningProcess) {
    Write-Host ""
    Write-Host "=== Game Launched Successfully ===" -ForegroundColor Green
    Write-Host "Process ID: $($RunningProcess.Id)"
    Write-Host "Process Name: $($RunningProcess.ProcessName)"
    Write-Host "Memory: $([math]::Round($RunningProcess.WorkingSet/1MB, 2)) MB"
    Write-Host "Window Handle: $($RunningProcess.MainWindowHandle)"

    # Verify session ID
    try {
        $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($RunningProcess.Id)"
        $procSessionId = $proc.SessionId
        Write-Host "Process Session ID: $procSessionId"

        if ($procSessionId -eq $consoleSessionId) {
            Write-Host "[OK] Process is running in console session $consoleSessionId" -ForegroundColor Green
        } else {
            Write-Warning "[!] Process is in session $procSessionId, expected $consoleSessionId"
        }
    } catch {
        Write-Host "Session verification: $_" -ForegroundColor Gray
    }

    Write-Host ""
    Write-Host "Window Handle indicates:" -ForegroundColor Cyan
    if ($RunningProcess.MainWindowHandle -eq 0) {
        Write-Warning "  Window is hidden/minimized (handle = 0)"
        Write-Host "  This may indicate the game is not fully visible on desktop"
    } else {
        Write-Host "  Window is visible (handle = $($RunningProcess.MainWindowHandle))" -ForegroundColor Green
    }
} else {
    Write-Warning "Process not found - game may have exited immediately or failed to start"
    Write-Host ""
    Write-Host "Checking task status..." -ForegroundColor Yellow
    $TaskInfo = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($TaskInfo) {
        $TaskStatus = Get-ScheduledTaskInfo -TaskName $TaskName
        Write-Host "Last Run Time: $($TaskStatus.LastRunTime)"
        Write-Host "Last Result: $($TaskStatus.LastTaskResult)"
        Write-Host "Number of Runs: $($TaskStatus.NumberOfMissedRuns)"
    }
}

# Clean up task
Write-Host ""
Write-Host "Cleaning up scheduled task..." -ForegroundColor Yellow
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
Write-Host "Done!" -ForegroundColor Green
