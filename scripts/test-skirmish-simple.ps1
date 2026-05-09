param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "Connecting to named pipe: \\.\pipe\$PipeName..." -ForegroundColor Yellow
$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    $pipe.Connect(5000)
    Write-Host "Connected!" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    Write-Host "`n>>> Sending Hello" -ForegroundColor Cyan
    $writer.WriteLine($hello)
    $helloResp = $reader.ReadLine()
    Write-Host "<<< Hello response: $($helloResp.Substring(0, [Math]::Min(100, $helloResp.Length)))..." -ForegroundColor Green

    Start-Sleep -Seconds 1

    # Test 1: Query skirmish setup
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 1: Query skirmish setup" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $query1 = '{"type":"SessionCommand","request_id":"test-1","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    Write-Host ">>> $query1" -ForegroundColor Cyan
    $writer.WriteLine($query1)
    $resp1 = $reader.ReadLine()
    Write-Host "<<< Response: $($resp1.Substring(0, [Math]::Min(200, $resp1.Length)))..." -ForegroundColor Green

    Start-Sleep -Seconds 1

    # Test 2: Set slot 1 to brutal AI
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 2: Set slot 1 to brutal AI" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $setSlot = '{"type":"SessionCommand","request_id":"test-2","cmd":"Skirmish.SetSlot","args":{"slot":1,"state":"brutal_ai","color":1,"template":1}}'
    Write-Host ">>> $setSlot" -ForegroundColor Cyan
    $writer.WriteLine($setSlot)
    $resp2 = $reader.ReadLine()
    Write-Host "<<< Response: $resp2" -ForegroundColor Green

    Start-Sleep -Seconds 1

    # Test 3: Set starting cash
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 3: Set starting cash to 10000" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $setCash = '{"type":"SessionCommand","request_id":"test-3","cmd":"Skirmish.SetStartingCash","args":{"cash":10000}}'
    Write-Host ">>> $setCash" -ForegroundColor Cyan
    $writer.WriteLine($setCash)
    $resp3 = $reader.ReadLine()
    Write-Host "<<< Response: $resp3" -ForegroundColor Green

    Start-Sleep -Seconds 1

    # Test 4: Query again to verify
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 4: Query skirmish setup again" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $query2 = '{"type":"SessionCommand","request_id":"test-4","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    Write-Host ">>> $query2" -ForegroundColor Cyan
    $writer.WriteLine($query2)
    $resp4 = $reader.ReadLine()
    Write-Host "<<< Response: $($resp4.Substring(0, [Math]::Min(200, $resp4.Length)))..." -ForegroundColor Green

    Write-Host "`n$('='*60)" -ForegroundColor Green
    Write-Host "All tests complete!" -ForegroundColor Green
    Write-Host "$('='*60)" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
    Write-Host "`nConnection closed." -ForegroundColor Yellow
}
