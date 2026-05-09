param(
    [string]$PipeName = "zh_ai_control",
    [string]$Mode = "autonomous",
    [string]$Profile = "sprawl_balanced",
    [double]$EconomyBias = 0.65,
    [double]$AggressionBias = 0.35,
    [int]$TargetPlayerIndex = 1
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Enabling Autonomy Mode" -ForegroundColor Cyan
Write-Host "Mode: $Mode" -ForegroundColor Yellow
Write-Host "Profile: $Profile" -ForegroundColor Yellow
Write-Host "Economy Bias: $EconomyBias" -ForegroundColor Yellow
Write-Host "Aggression Bias: $AggressionBias" -ForegroundColor Yellow
Write-Host "Target Player: $TargetPlayerIndex" -ForegroundColor Yellow
Write-Host "========================================`n" -ForegroundColor Cyan

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    Write-Host "Connecting to named pipe: \\.\pipe\$PipeName..." -ForegroundColor Gray
    $pipe.Connect(5000)
    Write-Host "Connected!" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Write-Host "Adapter connected!" -ForegroundColor Green
    Start-Sleep -Milliseconds 100

    # Set autonomy mode
    Write-Host "`n==> Setting Autonomy Mode..." -ForegroundColor Cyan
    $setMode = @{
        type = "SessionCommand"
        request_id = "autonomy-setmode-1"
        cmd = "Autonomy.SetMode"
        args = @{
            mode = $Mode
            enabled = $true
        }
    }

    $setModeJson = ConvertTo-Json -InputObject $setMode -Compress
    Write-Host "    Setting mode to: $Mode" -ForegroundColor Gray
    $writer.WriteLine($setModeJson)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Mode set successfully!" -ForegroundColor Green
    } else {
        Write-Host "    Failed to set mode: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }

    Start-Sleep -Milliseconds 100

    # Configure autonomy
    Write-Host "`n==> Configuring Autonomy..." -ForegroundColor Cyan
    $config = @{
        type = "SessionCommand"
        request_id = "autonomy-config-1"
        cmd = "Autonomy.Configure"
        args = @{
            profile = $Profile
            economy_bias = $EconomyBias
            aggression_bias = $AggressionBias
            defense_bias = 0.45
            expansion_bias = 0.55
            capture_tech = $true
            allow_superweapons = $false
            attack_automation_enabled = $true
            target_player_index = $TargetPlayerIndex
        }
    }

    $configJson = ConvertTo-Json -InputObject $config -Compress
    Write-Host "    Sending configuration..." -ForegroundColor Gray
    $writer.WriteLine($configJson)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Autonomy configured successfully!" -ForegroundColor Green
    } else {
        Write-Host "    Failed to configure: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }

    Start-Sleep -Milliseconds 100

    # Get status to verify
    Write-Host "`n==> Verifying Autonomy Status..." -ForegroundColor Cyan
    $status = '{"type":"SessionCommand","request_id":"status-1","cmd":"Autonomy.Status"}'
    $writer.WriteLine($status)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        $state = $respObj.result
        Write-Host "`nAutonomy Status:" -ForegroundColor Yellow
        Write-Host "  Mode: $($state.mode)" -ForegroundColor White
        Write-Host "  Profile: $($state.profile)" -ForegroundColor White
        Write-Host "  Paused: $($state.paused)" -ForegroundColor White
        Write-Host "  Economy Bias: $($state.economy_bias)" -ForegroundColor White
        Write-Host "  Aggression Bias: $($state.aggression_bias)" -ForegroundColor White
        Write-Host "  Attack Automation: $($state.attack_automation_enabled)" -ForegroundColor White
        Write-Host "  Target Player: $($state.target_player_index)" -ForegroundColor White
        Write-Host "  Capture Tech: $($state.capture_tech)" -ForegroundColor White
    }

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Autonomy Mode Enabled!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
} finally {
    if ($pipe -and $pipe.IsConnected) {
        Write-Host "Connection closed." -ForegroundColor Gray
        $pipe.Close()
    }
    $pipe.Dispose()
}
