param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Correct Faction Indices for GLA/China" -ForegroundColor Cyan

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    $pipe.Connect(5000)
    Write-Host "Connected!`n" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 1

    # Test index 1 for AI (should be template 2, which might be China)
    Write-Host "Testing AI with index 1 (template 2)..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"test-1","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1","index":1}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 1

    # Test index 2 for Player (should be template 3, which might be GLA)
    Write-Host "Testing Player with index 2 (template 3)..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"test-2","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate0","index":2}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 1

    Write-Host "`nDone - take screenshot to see factions" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
