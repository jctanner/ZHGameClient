param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Mapping Death Valley Position Buttons to Visual Locations" -ForegroundColor Cyan
Write-Host "    This will click each position button and query the result`n" -ForegroundColor White

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

    Write-Host "`nTesting position buttons 0-7...`n" -ForegroundColor Magenta

    for ($pos = 0; $pos -le 7; $pos++) {
        Write-Host "Position $pos" -ForegroundColor Cyan -NoNewline

        # Click the position button for player (slot 0)
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"click-$pos`",`"cmd`":`"Menu.Click`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition$pos`"}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        $respObj = ConvertFrom-Json $resp

        if (-not $respObj.ok) {
            Write-Host " - Button click failed: $($respObj.reason)" -ForegroundColor Red
            continue
        }

        Start-Sleep -Milliseconds 500

        # Query state to see which slot ended up at this position
        $query = '{"type":"SessionCommand","request_id":"query","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
        $writer.WriteLine($query)
        $resp = $reader.ReadLine()
        $queryObj = ConvertFrom-Json $resp

        if ($queryObj.ok) {
            $slot0Pos = $queryObj.result.slots[0].start_position
            $slot1Pos = $queryObj.result.slots[1].start_position
            Write-Host " - Player at position $slot0Pos, AI at position $slot1Pos" -ForegroundColor White
        } else {
            Write-Host " - Query failed" -ForegroundColor Red
        }

        Start-Sleep -Milliseconds 300
    }

    Write-Host "`nDone - take screenshot to see visual positions" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
