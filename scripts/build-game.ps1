param(
    [string]$BuildDir = "build/win32",
    [string]$Config = "Release",
    [ValidateSet("all", "zh", "generals", "configure-only", "build-only")]
    [string]$Target = "all",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host "==> $Name"
    & $Action
}

# Clean build directory if requested
if ($Clean -and (Test-Path $BuildDir)) {
    Invoke-Step "Clean" {
        Remove-Item -Recurse -Force $BuildDir
    }
}

# Configure step
if ($Target -ne "build-only") {
    Invoke-Step "Configure" {
        cmake -S . -B $BuildDir -A Win32
    }
}

# Build step
if ($Target -ne "configure-only") {
    switch ($Target) {
        "zh" {
            Invoke-Step "Build Zero Hour" {
                cmake --build $BuildDir --config $Config --target z_generals
            }
        }
        "generals" {
            Invoke-Step "Build Generals" {
                cmake --build $BuildDir --config $Config --target g_generals
            }
        }
        "all" {
            Invoke-Step "Build All" {
                cmake --build $BuildDir --config $Config
            }
        }
        "build-only" {
            Invoke-Step "Build All" {
                cmake --build $BuildDir --config $Config
            }
        }
    }
}

Write-Host ""
Write-Host "Build complete!"
Write-Host "Output directory: $BuildDir/$Config"
