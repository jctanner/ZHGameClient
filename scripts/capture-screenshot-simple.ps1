param(
    [string]$OutputPath = "D:\workspace\zero-hour-bot\ZHGameClient\screenshots",
    [string]$Filename = "screenshot-$(Get-Date -Format 'yyyyMMdd-HHmmss').png"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $OutputPath)) {
    New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
}

$FullPath = Join-Path $OutputPath $Filename
Write-Host "Capturing screenshot to: $FullPath" -ForegroundColor Cyan

$TaskName = "TempScreenshotCapture_$(Get-Date -Format 'yyyyMMddHHmmss')"

$CaptureScript = @"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Just capture the screen - don't try to focus
Start-Sleep -Milliseconds 500

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
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item $TempScriptPath -Force -ErrorAction SilentlyContinue
}
