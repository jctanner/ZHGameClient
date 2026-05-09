param(
    [string]$PipeName = "zh_ai_control",
    [int]$SprawlMultiplier = 10
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
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Play Game clicked!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }

    # Wait for game to load
    Write-Host "`n==> Waiting for game to load (30 seconds)..." -ForegroundColor Magenta
    for ($i = 30; $i -ge 1; $i--) {
        Write-Host "    $i..." -NoNewline -ForegroundColor Gray
        Start-Sleep -Seconds 1
    }
    Write-Host "`n    Game should be loaded!" -ForegroundColor Green

    # Configure autonomy with sprawl profile and multiplier
    Write-Host "`n==> Configuring autonomy (sprawl profile, ${SprawlMultiplier}x multiplier)..." -ForegroundColor Magenta
    $configAutonomy = "{`"type`":`"SessionCommand`",`"request_id`":`"config-1`",`"cmd`":`"Autonomy.Configure`",`"args`":{`"profile`":`"sprawl`",`"sprawl_multiplier`":$SprawlMultiplier}}"
    $writer.WriteLine($configAutonomy)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Autonomy configured!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }
    Start-Sleep -Milliseconds 500

    # Enable autonomy mode (autonomous = full autonomy)
    Write-Host "==> Enabling autonomous mode..." -ForegroundColor Magenta
    $setMode = '{"type":"SessionCommand","request_id":"mode-1","cmd":"Autonomy.SetMode","args":{"mode":"autonomous"}}'
    $writer.WriteLine($setMode)
    $resp = $reader.ReadLine()
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
    Write-Host "Profile: sprawl" -ForegroundColor Cyan
    Write-Host "Sprawl multiplier: ${SprawlMultiplier}x" -ForegroundColor Cyan
    Write-Host "`n"

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
