param(
    [string]$PipeName = "zh_ai_control",
    [int]$PlayerPos = 6,
    [int]$AIPos = 2
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Start Positions" -ForegroundColor Cyan
Write-Host "    Player: Position $PlayerPos" -ForegroundColor White
Write-Host "    AI: Position $AIPos`n" -ForegroundColor White

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

    # Set player position
    Write-Host "==> Setting player to position $PlayerPos..." -ForegroundColor Magenta
    $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"pos-player`",`"cmd`":`"Skirmish.SetSlot`",`"args`":{`"slot`":0,`"start_position`":$PlayerPos}}"
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Milliseconds 500

    # Set AI position
    Write-Host "==> Setting AI to position $AIPos..." -ForegroundColor Magenta
    $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"pos-ai`",`"cmd`":`"Skirmish.SetSlot`",`"args`":{`"slot`":1,`"start_position`":$AIPos}}"
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Milliseconds 500

    # Verify
    $query = '{"type":"SessionCommand","request_id":"verify","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    $writer.WriteLine($query)
    $resp = $reader.ReadLine()
    $queryObj = ConvertFrom-Json $resp

    if ($queryObj.ok) {
        Write-Host "`nVerified positions:" -ForegroundColor Green
        Write-Host "  Player: position $($queryObj.result.slots[0].start_position)" -ForegroundColor White
        Write-Host "  AI: position $($queryObj.result.slots[1].start_position)" -ForegroundColor White
    }

    Write-Host "`nDone - check game to see where they spawned" -ForegroundColor Cyan

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
