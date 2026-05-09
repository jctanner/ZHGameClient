param(
    [string]$PipeName = "zh_ai_control",
    [int]$PlayerPosition = 0,
    [int]$AIPosition = 7
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Position Button Clicks" -ForegroundColor Cyan
Write-Host "    Player -> Position $PlayerPosition" -ForegroundColor White
Write-Host "    AI -> Position $AIPosition`n" -ForegroundColor White

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
    Start-Sleep -Seconds 1

    # Clear existing positions by clicking them to remove players
    Write-Host "==> Clearing existing positions..." -ForegroundColor Magenta
    for ($i = 0; $i -le 7; $i++) {
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"clear-$i`",`"cmd`":`"Menu.Click`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition$i`"}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        Start-Sleep -Milliseconds 100
    }

    Start-Sleep -Milliseconds 500

    # Click player position
    Write-Host "==> Clicking position $PlayerPosition for player..." -ForegroundColor Magenta
    $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"player-pos`",`"cmd`":`"Menu.Click`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition$PlayerPosition`"}}"
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result - $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Milliseconds 500

    # Click AI position
    Write-Host "==> Clicking position $AIPosition for AI..." -ForegroundColor Magenta
    $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"ai-pos`",`"cmd`":`"Menu.Click`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition$AIPosition`"}}"
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result - $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Milliseconds 500

    # Verify
    $query = '{"type":"SessionCommand","request_id":"verify","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    $writer.WriteLine($query)
    $resp = $reader.ReadLine()
    $queryObj = ConvertFrom-Json $resp

    if ($queryObj.ok) {
        Write-Host "`nVerified positions:" -ForegroundColor Green
        Write-Host "  Player (Slot 0) - position $($queryObj.result.slots[0].start_position)" -ForegroundColor White
        Write-Host "  AI (Slot 1) - position $($queryObj.result.slots[1].start_position)" -ForegroundColor White
    }

    Write-Host "`nDone - check game screen for visual placement" -ForegroundColor Cyan

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
