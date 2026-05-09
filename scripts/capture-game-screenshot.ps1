param(
    [string]$OutputPath = "D:\workspace\zero-hour-bot\ZHGameClient\screenshots",
    [string]$ProcessName = "GameAdapter",
    [string]$Filename = "screenshot-$(Get-Date -Format 'yyyyMMdd-HHmmss').png"
)

$ErrorActionPreference = "Stop"

# Ensure output directory exists
if (-not (Test-Path $OutputPath)) {
    New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
}

$FullPath = Join-Path $OutputPath $Filename

Write-Host "Capturing screenshot of $ProcessName window..." -ForegroundColor Cyan

# Add necessary .NET assemblies
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

try {
    # Get the process
    $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

    if (-not $process) {
        Write-Host "Process $ProcessName not found, capturing entire screen instead..." -ForegroundColor Yellow

        # Capture entire screen
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen
        $bounds = $screen.Bounds
        $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $bitmap.Save($FullPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $bitmap.Dispose()

        Write-Host "Screenshot saved: $FullPath" -ForegroundColor Green
        Write-Host "Size: $($bounds.Width)x$($bounds.Height)" -ForegroundColor White
        return
    }

    # Get the main window handle
    $hwnd = $process.MainWindowHandle

    if ($hwnd -eq 0) {
        Write-Host "Window handle is 0, capturing entire screen..." -ForegroundColor Yellow

        # Capture entire screen
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen
        $bounds = $screen.Bounds
        $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $bitmap.Save($FullPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $bitmap.Dispose()

        Write-Host "Screenshot saved: $FullPath" -ForegroundColor Green
        Write-Host "Size: $($bounds.Width)x$($bounds.Height)" -ForegroundColor White
        return
    }

    # Add Win32 API calls
    Add-Type @"
        using System;
        using System.Runtime.InteropServices;
        public class Win32 {
            [StructLayout(LayoutKind.Sequential)]
            public struct RECT {
                public int Left;
                public int Top;
                public int Right;
                public int Bottom;
            }
            [DllImport("user32.dll")]
            public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
            [DllImport("user32.dll")]
            public static extern bool SetForegroundWindow(IntPtr hWnd);
        }
"@

    # Bring window to foreground
    [Win32]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 500

    # Get window rectangle
    $rect = New-Object Win32+RECT
    [Win32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top

    # Capture the window
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, [System.Drawing.Size]::new($width, $height))
    $bitmap.Save($FullPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $graphics.Dispose()
    $bitmap.Dispose()

    Write-Host "Screenshot saved: $FullPath" -ForegroundColor Green
    Write-Host "Window: $width x $height" -ForegroundColor White
    Write-Host "Location: ($($rect.Left), $($rect.Top))" -ForegroundColor White

} catch {
    Write-Host "Error capturing screenshot: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
