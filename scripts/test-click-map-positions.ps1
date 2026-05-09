param(
    [string]$PipeName = "zh_ai_control",
    [int]$TargetPosition = 6
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Map Position Button Clicks" -ForegroundColor Cyan
Write-Host "    Clicking position $TargetPosition`n" -ForegroundColor White

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

    # Click the map position button
    Write-Host "==> Clicking ButtonMapStartPosition$TargetPosition..." -ForegroundColor Magenta
    $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"click-pos`",`"cmd`":`"Menu.Click`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition$TargetPosition`"}}"
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    Write-Host "    Click result: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    if (-not $respObj.ok) {
        Write-Host "    Reason: $($respObj.reason)" -ForegroundColor Yellow
    }
    Start-Sleep -Seconds 1

    # Query state
    $query = '{"type":"SessionCommand","request_id":"verify","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    $writer.WriteLine($query)
    $resp = $reader.ReadLine()
    $queryObj = ConvertFrom-Json $resp

    if ($queryObj.ok) {
        Write-Host "`nCurrent positions:" -ForegroundColor Green
        Write-Host "  Player (Slot 0): position $($queryObj.result.slots[0].start_position)" -ForegroundColor White
        Write-Host "  AI (Slot 1): position $($queryObj.result.slots[1].start_position)" -ForegroundColor White
    }

    Write-Host "`nDone - check game screen" -ForegroundColor Cyan

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
