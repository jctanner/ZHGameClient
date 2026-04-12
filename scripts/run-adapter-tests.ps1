param(
    [string]$BuildDir = "build/win32",
    [string]$Config = "Release",
    [string]$Target = "z_gameengine_adapter_tests",
    [string]$CTestFilter = "z_gameengine_adapter_tests"
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

Invoke-Step "Configure" {
    cmake -S . -B $BuildDir -DBUILD_TESTING=ON
}

Invoke-Step "Build" {
    cmake --build $BuildDir --config $Config --target $Target
}

Invoke-Step "Test" {
    ctest --test-dir $BuildDir -C $Config -R $CTestFilter --output-on-failure
}
