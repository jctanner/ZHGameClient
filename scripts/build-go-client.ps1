param(
    [int]$BuildVersion = [int](Get-Date -Format "yyyyMMdd"),
    [ValidateSet("30", "60")]
    [string]$FpsMode = "60",
    [string]$Preset = "win32",
    [string]$RepoRoot,
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [bool]$BuildExtras = $true,
    [bool]$VerboseBuild = $true,
    [string]$LogPath = "logs\build-go-client.log",
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [switch]$SkipPackage
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-Step {
    param([string[]]$Command)
    Write-Host ">> $($Command -join ' ')" -ForegroundColor Cyan
    if ($Command.Length -eq 0) {
        throw "Invoke-Step received an empty command."
    }

    $useLog = -not [string]::IsNullOrWhiteSpace($script:ResolvedLogPath)
    if ($Command.Length -eq 1) {
        if ($useLog) {
            & $Command[0] 2>&1 | Tee-Object -FilePath $script:ResolvedLogPath -Append
        } else {
            & $Command[0]
        }
    } else {
        if ($useLog) {
            & $Command[0] $Command[1..($Command.Length - 1)] 2>&1 | Tee-Object -FilePath $script:ResolvedLogPath -Append
        } else {
            & $Command[0] $Command[1..($Command.Length - 1)]
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $($Command -join ' ')"
    }
}

function Set-GoFpsMode {
    param(
        [string]$HeaderPath,
        [string]$Mode
    )

    if (!(Test-Path $HeaderPath)) {
        throw "Cannot find FPS header: $HeaderPath"
    }

    $content = Get-Content -Path $HeaderPath -Raw
    $pattern = '(?m)^[ \t]*(?://)?[ \t]*#define[ \t]+GENERALS_ONLINE_HIGH_FPS_SERVER(?:[ \t]+1)?[ \t]*\r?$'

    if ($content -notmatch $pattern) {
        throw "Could not find GENERALS_ONLINE_HIGH_FPS_SERVER define in $HeaderPath. Update Set-GoFpsMode pattern if header format changed."
    }

    if ($Mode -eq "60") {
        $replacement = "#define GENERALS_ONLINE_HIGH_FPS_SERVER 1"
    } else {
        $replacement = "//#define GENERALS_ONLINE_HIGH_FPS_SERVER 1"
    }

    $updated = [regex]::Replace($content, $pattern, $replacement, 1)
    if ($updated -ne $content) {
        Set-Content -Path $HeaderPath -Value $updated -NoNewline
    }
}

function Reset-StaleCMakePresetBuild {
    param(
        [string]$RepoRoot,
        [string]$Preset
    )

    $buildDir = Join-Path $RepoRoot "build\$Preset"
    $cachePath = Join-Path $buildDir "CMakeCache.txt"

    if (!(Test-Path $cachePath)) {
        return
    }

    $cacheContent = Get-Content -Path $cachePath -Raw
    $expectedHome = (Resolve-Path $RepoRoot).Path
    $homeMatch = [regex]::Match($cacheContent, '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$')
    $actualHome = if ($homeMatch.Success) { $homeMatch.Groups[1].Value.Trim() } else { "" }

    if ([string]::IsNullOrWhiteSpace($actualHome)) {
        return
    }

    $expectedCanonical = [System.IO.Path]::GetFullPath($expectedHome).TrimEnd('\')
    $actualCanonical = [System.IO.Path]::GetFullPath($actualHome).TrimEnd('\')

    if ($expectedCanonical.ToLowerInvariant() -ne $actualCanonical.ToLowerInvariant()) {
        Write-Host "Detected stale CMake cache for preset '$Preset'." -ForegroundColor Yellow
        Write-Host "  Cache source:  $actualCanonical"
        Write-Host "  Current source: $expectedCanonical"
        Write-Host "Removing $buildDir to force a clean reconfigure..." -ForegroundColor Yellow
        Remove-Item -Path $buildDir -Recurse -Force
    }
}

function Assert-Win32ToolchainEnvironment {
    param([string]$Preset)

    if ($Preset -notlike "win32*") {
        return
    }

    $libEnv = $env:LIB
    if ([string]::IsNullOrWhiteSpace($libEnv)) {
        return
    }

    $segments = $libEnv.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    $hasX64 = $false
    $hasX86 = $false
    foreach ($seg in $segments) {
        $s = $seg.ToLowerInvariant()
        if ($s -match '\\x64($|\\)') { $hasX64 = $true }
        if ($s -match '\\x86($|\\)') { $hasX86 = $true }
    }

    if ($hasX64 -and -not $hasX86) {
        throw @"
Detected x64-only MSVC/Windows SDK library paths in LIB while using preset '$Preset'.
This causes linker failures like LNK4272 (x64 libs vs x86 target) and many unresolved externals.

Fix:
1) Open an x86 VS developer environment (e.g. "x86 Native Tools Command Prompt for VS 2022"), or run:
   `cmd /c ""%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x86 && powershell -NoExit"`
2) Re-run this script from that shell.
3) If build artifacts were generated in the wrong environment, delete `ZHGameClient\build\win32` and rebuild.
"@
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $candidateParent = Split-Path -Parent $scriptDir
    $candidateSelf = $scriptDir

    if (Test-Path (Join-Path $candidateParent "CMakeLists.txt")) {
        $repoRoot = (Resolve-Path $candidateParent).Path
    } elseif (Test-Path (Join-Path $candidateSelf "CMakeLists.txt")) {
        $repoRoot = (Resolve-Path $candidateSelf).Path
    } else {
        throw "Could not locate repo root from script path: $scriptDir"
    }
} else {
    $repoRoot = (Resolve-Path $RepoRoot).Path
}

if (!(Test-Path (Join-Path $repoRoot "CMakeLists.txt"))) {
    throw "RepoRoot does not look like a game repo (missing CMakeLists.txt): $repoRoot"
}

$fpsHeader = Join-Path $repoRoot "GeneralsMD\Code\GameEngine\Include\GameNetwork\GeneralsOnline\NextGenMP_defines.h"

Push-Location $repoRoot
try {
    $script:ResolvedLogPath = ""
    if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
        if ([System.IO.Path]::IsPathRooted($LogPath)) {
            $script:ResolvedLogPath = $LogPath
        } else {
            $script:ResolvedLogPath = Join-Path $repoRoot $LogPath
        }

        $logDir = Split-Path -Parent $script:ResolvedLogPath
        if (-not [string]::IsNullOrWhiteSpace($logDir) -and -not (Test-Path $logDir)) {
            New-Item -ItemType Directory -Path $logDir | Out-Null
        }

        $header = "[{0}] build-go-client.ps1 start" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
        Set-Content -Path $script:ResolvedLogPath -Value $header
        Write-Host "Logging command output to: $script:ResolvedLogPath" -ForegroundColor Yellow
    }

    Write-Host "Using build version: $BuildVersion" -ForegroundColor Yellow
    Write-Host "Setting Generals Online FPS mode to $FpsMode..." -ForegroundColor Yellow
    Set-GoFpsMode -HeaderPath $fpsHeader -Mode $FpsMode
    Assert-Win32ToolchainEnvironment -Preset $Preset

    $extrasSetting = if ($BuildExtras) { "ON" } else { "OFF" }
    $configureFlags = @(
        "--preset", $Preset,
        "-DRTS_BUILD_ZEROHOUR=ON",
        "-DRTS_BUILD_GENERALS=OFF",
        "-DRTS_BUILD_ZEROHOUR_EXTRAS=$extrasSetting",
        "-DRTS_BUILD_CORE_EXTRAS=$extrasSetting"
    )

    if (-not $SkipConfigure) {
        Reset-StaleCMakePresetBuild -RepoRoot $repoRoot -Preset $Preset
        Invoke-Step -Command (@("cmake") + $configureFlags)
    }

    $targets = @("z_generals")
    if ($BuildExtras) {
        $targets += "z_launcher"
    }

    if (-not $SkipBuild) {
        $buildCommand = @("cmake", "--build", "--preset", $Preset, "--config", $Configuration, "--target") + $targets
        if ($VerboseBuild) {
            $buildCommand += "--verbose"
        }
        Invoke-Step -Command $buildCommand
    }

    if (-not $SkipPackage) {
        $buildDir = "build\$Preset\GeneralsMD\$Configuration"
        $makePatchScript = Join-Path $repoRoot "MakePatch.ps1"
        Invoke-Step -Command @(
            "powershell",
            "-ExecutionPolicy", "Bypass",
            "-File", $makePatchScript,
            "-BuildVer", "$BuildVersion",
            "-BuildDir", $buildDir
        )
    }

    Write-Host ""
    Write-Host "Build wrapper completed." -ForegroundColor Green
    Write-Host "Expected outputs:"
    Write-Host "  build\$Preset\GeneralsMD\$Configuration\GeneralsOnlineZH.exe"
    if ($BuildExtras) {
        Write-Host "  build\$Preset\GeneralsMD\$Configuration\launcher.exe"
    }
    if (-not $SkipPackage) {
        Write-Host "  Patch\public_html\v$BuildVersion.zip"
        Write-Host "  Patch\public_html\updater\v$BuildVersion.gopatch"
        Write-Host "  Patch\private\genonlineserver\crcfiles\GeneralsOnlineZH.exe"
    }
}
finally {
    Pop-Location
}
