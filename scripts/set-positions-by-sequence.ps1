param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Setting Positions by Button Click Sequence" -ForegroundColor Cyan

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

    # Strategy: Click sequence to place player at desired position, then AI
    # From testing: clicking a position places "next selectable player" there
    # Player is slot 0 (first), AI is slot 1 (second)

    Write-Host "==> Clicking position 3 (should place player there)..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"click1","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition3"}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 500

    Write-Host "==> Clicking position 5 (should place AI there)..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"click2","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition5"}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 500

    # Verify
    $query = '{"type":"SessionCommand","request_id":"verify","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    $writer.WriteLine($query)
    $resp = $reader.ReadLine()
    $queryObj = ConvertFrom-Json $resp

    if ($queryObj.ok) {
        Write-Host "`nFinal positions:" -ForegroundColor Green
        Write-Host "  Player (Slot 0): position $($queryObj.result.slots[0].start_position)" -ForegroundColor White
        Write-Host "  AI (Slot 1): position $($queryObj.result.slots[1].start_position)" -ForegroundColor White
    }

    Write-Host "`nTake screenshot to verify visual placement" -ForegroundColor Cyan

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
