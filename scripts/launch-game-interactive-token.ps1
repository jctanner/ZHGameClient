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

Write-Host "=== Interactive Token Game Launcher ===" -ForegroundColor Cyan
Write-Host ""

# Check current session
$currentSession = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
Write-Host "Current PowerShell session: $currentSession" -ForegroundColor Yellow

# Query all sessions
$querySession = query session 2>$null
Write-Host ""
Write-Host "All sessions:"
Write-Host $querySession
Write-Host ""

# Find console session
$consoleSessionId = $null
foreach ($line in $querySession -split "`n") {
    if ($line -match 'console' -and $line -match 'Active') {
        if ($line -match '\s+(\d+)\s+Active') {
            $consoleSessionId = $matches[1]
            break
        }
    }
}

if ($consoleSessionId) {
    Write-Host "Target console session: $consoleSessionId" -ForegroundColor Green
} else {
    Write-Error "No active console session found"
    exit 1
}

if ($currentSession -eq 0 -and $consoleSessionId -ne 0) {
    Write-Host "NOTE: We are in Session 0, need to jump to Session $consoleSessionId" -ForegroundColor Yellow
}
Write-Host ""

# Create unique task name
$TaskName = "GameLauncher_IT_$(Get-Date -Format 'yyyyMMddHHmmss')"

# Get current user name (not domain\user, just user)
$userName = $env:USERNAME

Write-Host "Creating interactive scheduled task..." -ForegroundColor Yellow
Write-Host "Task name: $TaskName"
Write-Host "User: $userName"
Write-Host "Executable: $Executable"
Write-Host "Working dir: $GameDir"
Write-Host ""

# Create a temporary batch file to launch the game
$tempBat = Join-Path $env:TEMP "$TaskName.bat"
$batContent = @"
@echo off
cd /d "$GameDir"
start "" "$Executable" $Arguments
"@
Set-Content -Path $tempBat -Value $batContent -Encoding ASCII

Write-Host "Created launcher batch: $tempBat"
Write-Host ""

$cmdToRun = $tempBat

# Calculate start time (3 seconds from now)
$startTime = (Get-Date).AddSeconds(3)
$timeStr = $startTime.ToString("HH:mm:ss")
$dateStr = $startTime.ToString("MM/dd/yyyy")

# Delete existing task if present
try {
    schtasks /delete /tn "$TaskName" /f 2>&1 | Out-Null
} catch {
    # Task doesn't exist, that's fine
}

# Create the task using schtasks.exe with /IT flag
# /IT = Interactive - run in user's interactive session (not Session 0)
# /RU username = run as current user
# /RL HIGHEST = run with highest privileges
Write-Host "Running: schtasks /create with /IT flag" -ForegroundColor Cyan

# Reset LASTEXITCODE from previous commands
$global:LASTEXITCODE = 0

$result = schtasks /create `
    /tn "$TaskName" `
    /tr "$cmdToRun" `
    /sc once `
    /st $timeStr `
    /sd $dateStr `
    /ru $userName `
    /rl highest `
    /it `
    /f

if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to create scheduled task (exit code: $LASTEXITCODE)"
    exit 1
}

Write-Host "Task created successfully!" -ForegroundColor Green
Write-Host ""

# Wait for the task to run
Write-Host "Waiting for task to execute (5 seconds)..." -ForegroundColor Yellow
Start-Sleep -Seconds 5

# Check if process is running
$ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
$RunningProcess = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

if ($RunningProcess) {
    Write-Host ""
    Write-Host "=== SUCCESS ===" -ForegroundColor Green
    Write-Host "Process ID: $($RunningProcess.Id)"
    Write-Host "Memory: $([math]::Round($RunningProcess.WorkingSet/1MB, 2)) MB"
    Write-Host "Window Handle: $($RunningProcess.MainWindowHandle)"

    # Check session
    try {
        $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($RunningProcess.Id)"
        $procSessionId = $proc.SessionId
        Write-Host "Process Session ID: $procSessionId"

        if ($procSessionId -eq $consoleSessionId) {
            Write-Host ""
            Write-Host "*** PROCESS IN CORRECT SESSION ***" -ForegroundColor Green
            Write-Host "Process is in console session $consoleSessionId (interactive desktop)" -ForegroundColor Green
        } elseif ($procSessionId -eq 0) {
            Write-Host ""
            Write-Host "*** STILL IN SESSION 0 ***" -ForegroundColor Red
            Write-Warning "Process in Session 0 - /IT flag did not work"
        } else {
            Write-Warning "Process in session $procSessionId, expected $consoleSessionId"
        }
    } catch {
        Write-Host "Session verification error: $_"
    }

    if ($RunningProcess.MainWindowHandle -ne 0) {
        Write-Host ""
        Write-Host "*** WINDOW VISIBLE ***" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Warning "Window handle is 0 (hidden/minimized)"
    }
} else {
    Write-Warning "Process not found - may have crashed"

    # Check task info
    $TaskInfo = schtasks /query /tn "$TaskName" /fo csv /nh 2>$null
    if ($TaskInfo) {
        Write-Host "Task info: $TaskInfo"
    }

    # Try to get detailed task info
    try {
        $schedTaskInfo = Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction Stop
        Write-Host "Last Run: $($schedTaskInfo.LastRunTime)"
        Write-Host "Last Result: $($schedTaskInfo.LastTaskResult)"
        if ($schedTaskInfo.LastTaskResult -ne 0) {
            $hexCode = "0x{0:X}" -f $schedTaskInfo.LastTaskResult
            Write-Host "Error code: $hexCode"

            # Common error codes
            switch ($schedTaskInfo.LastTaskResult) {
                3221225477 { Write-Host "  = 0xC0000005 (Access Violation / Crash)" }
                2147943785 { Write-Host "  = 0x80070005 (Access Denied)" }
            }
        }
    } catch {
        Write-Host "Could not get task info: $_"
    }
}

# Clean up
Write-Host ""
Write-Host "Cleaning up..." -ForegroundColor Yellow
schtasks /delete /tn "$TaskName" /f 2>$null | Out-Null
if (Test-Path $tempBat) {
    Remove-Item $tempBat -Force -ErrorAction SilentlyContinue
}
Write-Host "Done!" -ForegroundColor Green
