param(
    [string]$BuildDir = "build/win32",
    [string]$Config = "Release",
    [string]$Target = "z_gameengine_adapter_tests z_gameengine_adapter_ui_tests",
    [string]$CTestFilter = "z_gameengine_adapter"
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
