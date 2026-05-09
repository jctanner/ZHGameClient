param(
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [string]$Executable = "GameAdapter.exe",
    [string]$Arguments = "-win"
)

$ErrorActionPreference = "Stop"

$GamePath = Join-Path $GameDir $Executable

if (-not (Test-Path $GamePath)) {
    Write-Error "Game executable not found: $GamePath"
    exit 1
}

Write-Host "=== InteractiveOrPassword Logon Launcher ===" -ForegroundColor Cyan
Write-Host ""

# Check sessions
$currentSession = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
Write-Host "Current session: $currentSession"

$querySession = query session 2>$null
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
    Write-Error "No active console session"
    exit 1
}
Write-Host ""

# Task name
$TaskName = "GameLauncher_IOP_$(Get-Date -Format 'yyyyMMddHHmmss')"

Write-Host "Creating task: $TaskName" -ForegroundColor Yellow
Write-Host "Executable: $GamePath"
Write-Host "Arguments: $Arguments"
Write-Host ""

# Create action
$Action = New-ScheduledTaskAction -Execute $GamePath -Argument $Arguments -WorkingDirectory $GameDir

# Create trigger
$Trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(3)

# Settings
$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Hours 4)

# CRITICAL: Use InteractiveOrPassword logon type
# This is specifically for tasks that need to run in the interactive session
$CurrentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
Write-Host "Creating principal for: $CurrentUser"

$Principal = New-ScheduledTaskPrincipal `
    -UserId $CurrentUser `
    -LogonType InteractiveOrPassword `
    -RunLevel Highest

Write-Host "Logon Type: InteractiveOrPassword" -ForegroundColor Cyan
Write-Host "Run Level: Highest"
Write-Host ""

# Register task
try {
    $Task = Register-ScheduledTask `
        -TaskName $TaskName `
        -Action $Action `
        -Trigger $Trigger `
        -Settings $Settings `
        -Principal $Principal `
        -Force

    Write-Host "Task registered!" -ForegroundColor Green
} catch {
    Write-Error "Failed to register task: $_"
    exit 1
}

# Start immediately
Write-Host "Starting task..." -ForegroundColor Yellow
try {
    Start-ScheduledTask -TaskName $TaskName
    Write-Host "Task started!" -ForegroundColor Green
} catch {
    Write-Warning "Start failed: $_"
}

# Wait
Write-Host ""
Write-Host "Waiting 7 seconds..." -ForegroundColor Yellow
Start-Sleep -Seconds 7

# Check process
$ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
$RunningProcess = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

if ($RunningProcess) {
    Write-Host ""
    Write-Host "=== PROCESS FOUND ===" -ForegroundColor Green
    Write-Host "Process ID: $($RunningProcess.Id)"
    Write-Host "Memory: $([math]::Round($RunningProcess.WorkingSet/1MB, 2)) MB"
    Write-Host "Window Handle: $($RunningProcess.MainWindowHandle)"

    # Check session
    try {
        $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($RunningProcess.Id)"
        $procSessionId = $proc.SessionId
        Write-Host "Process Session ID: $procSessionId"
        Write-Host ""

        if ($procSessionId -eq $consoleSessionId) {
            Write-Host "************************************" -ForegroundColor Green
            Write-Host "*** SUCCESS: IN CONSOLE SESSION ***" -ForegroundColor Green
            Write-Host "************************************" -ForegroundColor Green
        } elseif ($procSessionId -eq 0) {
            Write-Host "************************************" -ForegroundColor Red
            Write-Host "*** FAILED: STILL IN SESSION 0  ***" -ForegroundColor Red
            Write-Host "************************************" -ForegroundColor Red
        } else {
            Write-Warning "In session $procSessionId (expected $consoleSessionId)"
        }
    } catch {
        Write-Host "Session check error: $_"
    }

    Write-Host ""
    if ($RunningProcess.MainWindowHandle -ne 0) {
        Write-Host "*** WINDOW VISIBLE (handle: $($RunningProcess.MainWindowHandle)) ***" -ForegroundColor Green
    } else {
        Write-Warning "Window hidden (handle = 0)"
    }
} else {
    Write-Warning "Process not found"

    try {
        $TaskInfo = Get-ScheduledTaskInfo -TaskName $TaskName
        Write-Host "Last Run: $($TaskInfo.LastRunTime)"
        Write-Host "Last Result: $($TaskInfo.LastTaskResult)"
        if ($TaskInfo.LastTaskResult -ne 0) {
            Write-Host "Error: 0x$([Convert]::ToString($TaskInfo.LastTaskResult, 16))"
        }
    } catch {
        Write-Host "Task info error: $_"
    }
}

# Cleanup
Write-Host ""
Write-Host "Cleaning up..." -ForegroundColor Yellow
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
Write-Host "Done!" -ForegroundColor Green
