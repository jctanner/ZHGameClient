param(
    [string]$GameDir = "D:\SteamLibrary\steamapps\common\Command & Conquer Generals - Zero Hour",
    [string]$Executable = "GameAdapter.exe",
    [string]$Arguments = "-win -xres 1920 -yres 1080"
)

$ErrorActionPreference = "Stop"

$GamePath = Join-Path $GameDir $Executable

if (-not (Test-Path $GamePath)) {
    Write-Error "Game executable not found: $GamePath"
    exit 1
}

Write-Host "=== WMI Session-Targeted Launcher ===" -ForegroundColor Cyan
Write-Host ""

# Check current session
$currentSession = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
Write-Host "Current PowerShell session: $currentSession"

# Query sessions
$querySession = query session 2>$null
Write-Host ""
Write-Host "Available sessions:"
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

if (-not $consoleSessionId) {
    Write-Error "No active console session found"
    exit 1
}

Write-Host "Target session: $consoleSessionId (console)" -ForegroundColor Green
Write-Host ""

# Create a batch file to launch the game with NVIDIA GPU preference
$tempBat = Join-Path $env:TEMP "game_launcher_$(Get-Random).bat"
$batContent = @"
@echo off
REM Force NVIDIA GPU on Optimus systems
set SHIM_RENDERING_MODE=0
set SHIM_MCCOMPAT=0x800000001
cd /d "$GameDir"
start "" "$Executable" $Arguments
"@
Set-Content -Path $tempBat -Value $batContent -Encoding ASCII

Write-Host "Created launcher: $tempBat"
Write-Host ""

# Use WMI to create a process in the specific session
Write-Host "Attempting to launch via WMI in session $consoleSessionId..." -ForegroundColor Yellow

try {
    # Unfortunately, WMI's Win32_Process.Create() doesn't have a direct session parameter
    # But we can try using a scheduled task with "At startup" or "At logon" trigger
    # which forces it to run in the user's session

    Write-Host "Creating logon-triggered scheduled task..." -ForegroundColor Cyan

    $TaskName = "GameLauncher_Logon_$(Get-Date -Format 'yyyyMMddHHmmss')"

    # Create action
    $Action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c `"$tempBat`""

    # Use AtLogOn trigger for current user - this should force interactive session
    $Trigger = New-ScheduledTaskTrigger -AtLogOn

    # Settings
    $Settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -StartWhenAvailable `
        -MultipleInstances IgnoreNew `
        -ExecutionTimeLimit (New-TimeSpan -Hours 4)

    # Use current user
    $CurrentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

    # Try Interactive logon type (simpler than InteractiveOrPassword)
    $Principal = New-ScheduledTaskPrincipal `
        -UserId $CurrentUser `
        -LogonType Interactive `
        -RunLevel Highest

    Write-Host "Registering task: $TaskName"
    Write-Host "User: $CurrentUser"
    Write-Host "Trigger: AtLogOn (should force interactive session)"
    Write-Host ""

    $Task = Register-ScheduledTask `
        -TaskName $TaskName `
        -Action $Action `
        -Trigger $Trigger `
        -Settings $Settings `
        -Principal $Principal `
        -Force

    Write-Host "Task registered! Starting now..." -ForegroundColor Green

    # Start the task immediately (even though it's an AtLogOn trigger)
    Start-ScheduledTask -TaskName $TaskName

    Write-Host "Task started! Waiting for process..." -ForegroundColor Yellow
    Start-Sleep -Seconds 5

    # Check for process
    $ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
    $RunningProcesses = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)

    if ($RunningProcesses.Count -gt 0) {
        Write-Host ""
        Write-Host "=== PROCESS RUNNING ($($RunningProcesses.Count) instance(s)) ===" -ForegroundColor Green

        # Check each process
        $successCount = 0
        foreach ($RunningProcess in $RunningProcesses) {
            Write-Host ""
            Write-Host "Process ID: $($RunningProcess.Id)" -ForegroundColor Cyan
            Write-Host "Memory: $([math]::Round($RunningProcess.WorkingSet/1MB, 2)) MB"
            Write-Host "Window Handle: $($RunningProcess.MainWindowHandle)"

            # Check session
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($RunningProcess.Id)"
            $procSessionId = $proc.SessionId
            Write-Host "Process Session ID: $procSessionId"

            if ($procSessionId -eq $consoleSessionId) {
                Write-Host "[OK] In console session $consoleSessionId" -ForegroundColor Green
                $successCount++
            } elseif ($procSessionId -eq 0) {
                Write-Host "[!] In Session 0 (non-interactive)" -ForegroundColor Red
            } else {
                Write-Host "[?] In session $procSessionId (expected $consoleSessionId)" -ForegroundColor Yellow
            }

            if ($RunningProcess.MainWindowHandle -ne 0) {
                Write-Host "[OK] Window visible (handle: $($RunningProcess.MainWindowHandle))" -ForegroundColor Green
            } else {
                Write-Host "[!] Window hidden (handle = 0)" -ForegroundColor Yellow
            }
        }

        Write-Host ""
        if ($successCount -gt 0) {
            Write-Host "********************************************" -ForegroundColor Green
            Write-Host "*** SUCCESS: $successCount process(es) in console session ***" -ForegroundColor Green
            Write-Host "********************************************" -ForegroundColor Green
            $result = "SUCCESS"
        } else {
            Write-Host "********************************************" -ForegroundColor Red
            Write-Host "*** FAILED: All processes in wrong session ***" -ForegroundColor Red
            Write-Host "********************************************" -ForegroundColor Red
            $result = "PARTIAL"
        }

    } else {
        Write-Warning "Process not found"

        $TaskInfo = Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction SilentlyContinue
        if ($TaskInfo) {
            Write-Host "Task Last Result: $($TaskInfo.LastTaskResult)"
            if ($TaskInfo.LastTaskResult -ne 0) {
                Write-Host "Error: 0x$([Convert]::ToString($TaskInfo.LastTaskResult, 16))"
            }
        }

        $result = "FAILED"
    }

    # Cleanup task
    Write-Host ""
    Write-Host "Cleaning up task..." -ForegroundColor Yellow
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue

} catch {
    Write-Error "Launch failed: $_"
    $result = "ERROR"
} finally {
    # Clean up batch file
    if (Test-Path $tempBat) {
        Start-Sleep -Seconds 2  # Give it time to execute
        Remove-Item $tempBat -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "Done! Result: $result" -ForegroundColor $(if ($result -eq "SUCCESS") { "Green" } else { "Yellow" })
