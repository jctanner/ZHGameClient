param(
    [string]$PipeName = "zh_ai_control",
    [ValidateSet("sprawl", "sprawl_balanced", "aggressive", "defensive", "economic", "tech", "standard")]
    [string]$Profile = "sprawl",
    [int]$SprawlMultiplier = 10,
    [bool]$AttackEnabled = $true
    # Note: army_cap and produce_units control require C++ adapter changes
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Start Game and Enable Autonomy" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    $pipe.Connect(5000)
    Write-Host "Connected!" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 500

    # Click "Play Game" button
    Write-Host "==> Clicking Play Game button..." -ForegroundColor Magenta
    $clickStart = '{"type":"SessionCommand","request_id":"start-1","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonStart"}}'
    $writer.WriteLine($clickStart)
    $resp = $reader.ReadLine()

    if ($resp) {
        $respObj = ConvertFrom-Json $resp
        if ($respObj.ok) {
            Write-Host "    Play Game clicked!" -ForegroundColor Green
        } else {
            Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "    Play Game clicked (no response, game transitioning)" -ForegroundColor Yellow
    }

    # Wait for game to transition from menu to in-game
    Write-Host "`n==> Waiting for game to transition (8 seconds)..." -ForegroundColor Magenta
    for ($i = 8; $i -ge 1; $i--) {
        Write-Host "    $i..." -NoNewline -ForegroundColor Gray
        Start-Sleep -Seconds 1
    }
    Write-Host "`n    Transition complete!" -ForegroundColor Green

    # Reconnect to pipe after game transition
    Write-Host "`n==> Reconnecting to pipe after game load..." -ForegroundColor Magenta
    $pipe.Close()
    $pipe.Dispose()

    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $pipe.Connect(5000)
        Write-Host "    Reconnected!" -ForegroundColor Green
    } catch {
        Write-Host "    Failed to reconnect: $_" -ForegroundColor Red
        exit 1
    }

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello again
    $hello = '{"type":"Hello","request_id":"hello-2"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 200

    # Set camera height higher to see more of the map
    Write-Host "`n==> Setting camera height..." -ForegroundColor Magenta
    $cameraCmd = '{"type":"SessionCommand","request_id":"camera-1","cmd":"Game.Camera.Set","args":{"height_multiplier":2.5}}'
    $writer.WriteLine($cameraCmd)
    $resp = $reader.ReadLine()

    if ($resp) {
        $respObj = ConvertFrom-Json $resp
        if ($respObj.ok) {
            Write-Host "    Camera height set to 2.5x!" -ForegroundColor Green
        } else {
            Write-Host "    Warning: Camera adjustment failed (may not be critical)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "    Warning: No response from camera command" -ForegroundColor Yellow
    }
    Start-Sleep -Milliseconds 200

    # Configure autonomy
    $attackStatus = if ($AttackEnabled) { "enabled" } else { "disabled" }
    Write-Host "`n==> Configuring autonomy ($Profile profile, ${SprawlMultiplier}x multiplier, attacks ${attackStatus})..." -ForegroundColor Magenta
    $attackEnabledJson = if ($AttackEnabled) { "true" } else { "false" }
    $configAutonomy = "{`"type`":`"SessionCommand`",`"request_id`":`"config-1`",`"cmd`":`"Autonomy.Configure`",`"args`":{`"profile`":`"$Profile`",`"sprawl_multiplier`":$SprawlMultiplier,`"attack_enabled`":$attackEnabledJson}}"
    $writer.WriteLine($configAutonomy)
    $resp = $reader.ReadLine()

    if (-not $resp) {
        Write-Host "    Failed: No response from adapter (pipe disconnected?)" -ForegroundColor Red
        exit 1
    }

    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Autonomy configured!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }
    Start-Sleep -Milliseconds 500

    # Enable autonomy mode (autonomous = full autonomy)
    Write-Host "`n==> Enabling autonomous mode..." -ForegroundColor Magenta
    $setMode = '{"type":"SessionCommand","request_id":"mode-1","cmd":"Autonomy.SetMode","args":{"mode":"autonomous"}}'
    $writer.WriteLine($setMode)
    $resp = $reader.ReadLine()

    if (-not $resp) {
        Write-Host "    Failed: No response from adapter (pipe disconnected?)" -ForegroundColor Red
        exit 1
    }

    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Autonomy mode enabled!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Game started with autonomy!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green
    Write-Host "Profile: $Profile" -ForegroundColor Cyan
    Write-Host "Sprawl multiplier: ${SprawlMultiplier}x" -ForegroundColor Cyan
    Write-Host "Attacks: $attackStatus" -ForegroundColor Cyan

    # Show profile-specific info
    if ($Profile -eq "sprawl_balanced") {
        Write-Host "`nNote: sprawl_balanced caps at 100 units and pauses production smartly" -ForegroundColor Yellow
    } elseif ($Profile -eq "sprawl") {
        Write-Host "`nNote: sprawl has no unit cap (9999) - will produce units continuously" -ForegroundColor Yellow
    }
    Write-Host "`n"

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
