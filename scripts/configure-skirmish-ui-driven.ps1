param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "UI-Driven Skirmish Configuration" -ForegroundColor Cyan
Write-Host "Death Valley: Player (GLA) vs Easy AI (China)" -ForegroundColor Cyan
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
    $helloResp = $reader.ReadLine()
    Start-Sleep -Seconds 1

    # Navigate to Skirmish Menu
    Write-Host "==> Navigating to Skirmish Menu..." -ForegroundColor Magenta
    $clickSP = '{"type":"SessionCommand","request_id":"nav-1","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSinglePlayer"}}'
    $writer.WriteLine($clickSP)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    $clickSkirmish = '{"type":"SessionCommand","request_id":"nav-2","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSkirmish"}}'
    $writer.WriteLine($clickSkirmish)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 3
    Write-Host "    At skirmish menu`n" -ForegroundColor Green

    # Configure Player (Slot 1 in UI = index 1)
    # First we need to understand the combo box numbering
    # Based on the screenshot, it looks like ComboBoxPlayerTemplate1 is the first player slot

    Write-Host "==> Configuring Player Slot (testing different indices)..." -ForegroundColor Magenta

    # Test: Try to set player faction to GLA
    # We know index 2 gives China, so let's try other indices
    $testIndices = @(0, 1, 2, 3, 4)

    foreach ($idx in $testIndices) {
        Write-Host "  Trying index $idx for Player faction..." -ForegroundColor Yellow
        $select = "{`"type`":`"SessionCommand`",`"request_id`":`"test-$idx`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1`",`"index`":$idx}}"
        $writer.WriteLine($select)
        $resp = $reader.ReadLine()
        $respObj = ConvertFrom-Json $resp

        if ($respObj.ok) {
            Write-Host "    Index $idx: SUCCESS" -ForegroundColor Green
        } else {
            Write-Host "    Index $idx: FAILED - $($respObj.reason)" -ForegroundColor Red
            break
        }
        Start-Sleep -Milliseconds 500
    }

    Write-Host "`n==> Current state - please check game window" -ForegroundColor Cyan
    Write-Host "    Last selection was index 4" -ForegroundColor White
    Write-Host "`nTest complete - ready for screenshot" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
