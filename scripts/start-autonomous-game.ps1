param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Starting Autonomous Skirmish Game" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    Write-Host "Connecting to game..." -ForegroundColor Gray
    $pipe.Connect(5000)
    Write-Host "Connected!" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 100

    # Step 1: Enable autonomy mode
    Write-Host "`n==> Step 1: Enabling Autonomy Mode..." -ForegroundColor Cyan
    $setMode = @{
        type = "SessionCommand"
        request_id = "setmode-1"
        cmd = "Autonomy.SetMode"
        args = @{
            mode = "autonomous"
        }
    }
    $writer.WriteLine((ConvertTo-Json -InputObject $setMode -Compress))
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 100

    $config = @{
        type = "SessionCommand"
        request_id = "config-1"
        cmd = "Autonomy.Configure"
        args = @{
            profile = "sprawl_balanced"
            economy_bias = 0.65
            aggression_bias = 0.35
            capture_tech = $true
            attack_automation_enabled = $true
            target_player_index = 1
        }
    }
    $writer.WriteLine((ConvertTo-Json -InputObject $config -Compress))
    $resp = $reader.ReadLine()
    Write-Host "    Autonomy configured!" -ForegroundColor Green
    Start-Sleep -Milliseconds 100

    # Step 1b: Resume autonomy (critical - starts the autonomy loop!)
    Write-Host "`n==> Step 1b: Resuming Autonomy..." -ForegroundColor Cyan
    $resume = @{
        type = "SessionCommand"
        request_id = "resume-1"
        cmd = "Autonomy.Resume"
        args = @{}
    }
    $writer.WriteLine((ConvertTo-Json -InputObject $resume -Compress))
    $resp = $reader.ReadLine()
    Write-Host "    Autonomy resumed!" -ForegroundColor Green
    Start-Sleep -Milliseconds 100

    # Step 2: Start the game (internal state)
    Write-Host "`n==> Step 2: Starting Game (internal)..." -ForegroundColor Cyan
    $start = '{"type":"SessionCommand","request_id":"start-1","cmd":"Skirmish.Start"}'
    $writer.WriteLine($start)
    $resp = $reader.ReadLine()
    Write-Host "    Game state set to starting!" -ForegroundColor Green
    Start-Sleep -Milliseconds 500

    # Step 3: Click the Start button in UI
    Write-Host "`n==> Step 3: Clicking Start Button..." -ForegroundColor Cyan
    $click = @{
        type = "SessionCommand"
        request_id = "click-1"
        cmd = "Menu.Click"
        args = @{
            controlId = "SkirmishGameOptionsMenu.wnd:ButtonStart"
        }
    }
    $writer.WriteLine((ConvertTo-Json -InputObject $click -Compress))
    $resp = $reader.ReadLine()

    if ($null -ne $resp -and $resp -ne "") {
        $respObj = ConvertFrom-Json $resp
        if ($respObj.ok) {
            Write-Host "    Start button clicked!" -ForegroundColor Green
        } else {
            Write-Host "    Click failed: $($respObj.reason)" -ForegroundColor Red
        }
    } else {
        Write-Host "    No response (game may have transitioned)" -ForegroundColor Yellow
    }

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Autonomous Game Started!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
