param(
    [string]$BuildDir = "build/win32",
    [string]$Config = "Release",
    [string]$Target = "z_gameengine_adapter_tests z_gameengine_adapter_ui_tests z_gameengine_adapter_telemetry_tests",
    [string]$CTestFilter = "z_gameengine_adapter"
)

$ErrorActionPreference = "Stop"

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

Invoke-Step "Configure" {
    cmake -S . -B $BuildDir -A Win32 -DBUILD_TESTING=ON
}

Invoke-Step "Build" {
    # Build both adapter test executables
    foreach ($t in $Target -split ' ') {
        if ($t) {
            cmake --build $BuildDir --config $Config --target $t
        }
    }
}

Invoke-Step "Test" {
    ctest --test-dir $BuildDir -C $Config -R $CTestFilter --output-on-failure
}
