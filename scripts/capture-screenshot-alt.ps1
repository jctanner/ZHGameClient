param(
    [string]$OutputPath = "D:\workspace\zero-hour-bot\ZHGameClient\screenshots",
    [string]$Filename = "screenshot-$(Get-Date -Format 'yyyyMMdd-HHmmss').png"
)

$ErrorActionPreference = "Stop"

# Ensure output directory exists
if (-not (Test-Path $OutputPath)) {
    New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
}

$FullPath = Join-Path $OutputPath $Filename

Write-Host "Capturing screenshot to: $FullPath" -ForegroundColor Cyan

# Use a scheduled task to capture screenshot in the interactive session
$TaskName = "TempScreenshotCapture_$(Get-Date -Format 'yyyyMMddHHmmss')"

# Create a script that will run in the user session
$CaptureScript = @"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Try to bring GameAdapter window to foreground
Add-Type @'
    using System;
    using System.Runtime.InteropServices;
    using System.Text;
    public class WinAPI {
        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")]
        public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        [DllImport("user32.dll")]
        public static extern bool IsIconic(IntPtr hWnd);
        public const int SW_RESTORE = 9;
    }
'@

# Try multiple window titles
`$windowTitles = @(
    "Command & Conquer Generals Zero Hour",
    "Generals Zero Hour",
    "Command and Conquer Generals Zero Hour"
)

`$hwnd = [IntPtr]::Zero
foreach (`$title in `$windowTitles) {
    `$hwnd = [WinAPI]::FindWindow(`$null, `$title)
    if (`$hwnd -ne [IntPtr]::Zero) {
        break
    }
}

if (`$hwnd -ne [IntPtr]::Zero) {
    if ([WinAPI]::IsIconic(`$hwnd)) {
        [WinAPI]::ShowWindow(`$hwnd, [WinAPI]::SW_RESTORE) | Out-Null
    }
    [WinAPI]::SetForegroundWindow(`$hwnd) | Out-Null
    Start-Sleep -Seconds 1
}

`$screen = [System.Windows.Forms.Screen]::PrimaryScreen
`$bounds = `$screen.Bounds
`$bitmap = New-Object System.Drawing.Bitmap `$bounds.Width, `$bounds.Height
`$graphics = [System.Drawing.Graphics]::FromImage(`$bitmap)
`$graphics.CopyFromScreen(`$bounds.Location, [System.Drawing.Point]::Empty, `$bounds.Size)
`$bitmap.Save('$FullPath', [System.Drawing.Imaging.ImageFormat]::Png)
`$graphics.Dispose()
`$bitmap.Dispose()
"@

$TempScriptPath = Join-Path $env:TEMP "capture_screenshot_temp.ps1"
Set-Content -Path $TempScriptPath -Value $CaptureScript

try {
    # Create scheduled task to run in user session
    $Action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$TempScriptPath`""
    $Trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(2)
    $Settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable

    Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings -Force | Out-Null

    Write-Host "Waiting for screenshot capture..." -ForegroundColor Yellow
    Start-Sleep -Seconds 4

    if (Test-Path $FullPath) {
        $fileInfo = Get-Item $FullPath
        Write-Host "Screenshot saved: $FullPath" -ForegroundColor Green
        Write-Host "Size: $([math]::Round($fileInfo.Length / 1KB, 2)) KB" -ForegroundColor White
    } else {
        Write-Host "ERROR: Screenshot file not created" -ForegroundColor Red
    }

} finally {
    # Clean up
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item $TempScriptPath -Force -ErrorAction SilentlyContinue
}
