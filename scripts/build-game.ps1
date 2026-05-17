param(
    [string]$BuildDir = "build/win32",
    [string]$Config = "Release",
    [ValidateSet("all", "zh", "generals", "configure-only", "build-only")]
    [string]$Target = "all",
    [switch]$Clean,
    [string]$LogFile = ""
)

$ErrorActionPreference = "Stop"

if ($LogFile) {
    $LogDir = Split-Path -Parent $LogFile
    if ($LogDir -and -not (Test-Path $LogDir)) {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    }
    Start-Transcript -Path $LogFile -Append | Out-Null
    Write-Host "Logging to: $LogFile"
    Write-Host "=== build start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ==="
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host "==> $Name"
    $script:LASTEXITCODE = $null
    & $Action
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "Step '$Name' failed with exit code $LASTEXITCODE"
    }
}

function Resolve-ZHExecutable {
    param(
        [string]$BuildDir,
        [string]$Config
    )

    $candidates = @(
        (Join-Path $BuildDir "GeneralsMD\$Config\GeneralsOnlineZH.exe"),
        (Join-Path $BuildDir "GeneralsMD\GeneralsOnlineZH.exe"),
        (Join-Path $BuildDir "$Config\GeneralsOnlineZH.exe"),
        (Join-Path $BuildDir "GeneralsOnlineZH.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return @{ Path = $candidate; Candidates = $candidates; ResolvedBySearch = $false }
        }
    }

    $found = Get-ChildItem -Path $BuildDir -Filter GeneralsOnlineZH.exe -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($found) {
        return @{ Path = $found.FullName; Candidates = $candidates; ResolvedBySearch = $true }
    }

    return @{ Path = $null; Candidates = $candidates; ResolvedBySearch = $false }
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
                cmake --build $BuildDir --config $Config --target z_generals --verbose -- /v:detailed
            }
        }
        "generals" {
            Invoke-Step "Build Generals" {
                cmake --build $BuildDir --config $Config --target g_generals --verbose -- /v:detailed
            }
        }
        "all" {
            Invoke-Step "Build All" {
                cmake --build $BuildDir --config $Config --verbose -- /v:detailed
            }
        }
        "build-only" {
            Invoke-Step "Build All" {
                cmake --build $BuildDir --config $Config --verbose -- /v:detailed
            }
        }
    }
}

Write-Host ""
Write-Host "Build complete!"
Write-Host "Output directory: $BuildDir/$Config"

if ($Target -eq "zh" -or $Target -eq "all" -or $Target -eq "build-only") {
    $resolved = Resolve-ZHExecutable -BuildDir $BuildDir -Config $Config
    if (-not $resolved.Path) {
        Write-Host "Checked executable candidates:"
        $resolved.Candidates | ForEach-Object { Write-Host "  $_" }
        throw "GeneralsOnlineZH.exe was not produced under build dir: $BuildDir"
    }

    Write-Host "Resolved Zero Hour executable: $($resolved.Path)"
    if ($resolved.ResolvedBySearch) {
        Write-Host "Resolved via recursive search under build dir"
    }
}

if ($LogFile) {
    Stop-Transcript | Out-Null
}
