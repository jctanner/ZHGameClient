param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Setup 1v1 Factions and Positions" -ForegroundColor Cyan
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

    # Set Player 0 faction to GLA (template index 3)
    Write-Host "==> Setting Player 0 faction to GLA..." -ForegroundColor Magenta
    $setFaction0 = '{"type":"SessionCommand","request_id":"faction-0","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate0","index":3}}'
    $writer.WriteLine($setFaction0)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Player 0 set to GLA!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }
    Start-Sleep -Milliseconds 500

    # Set Player 1 faction to China (template index 2)
    Write-Host "==> Setting Player 1 faction to China..." -ForegroundColor Magenta
    $setFaction1 = '{"type":"SessionCommand","request_id":"faction-1","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1","index":2}}'
    $writer.WriteLine($setFaction1)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Player 1 set to China!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }
    Start-Sleep -Milliseconds 500

    # Click position 4 button first to assign local player
    Write-Host "==> Clicking map start position 4..." -ForegroundColor Magenta
    $clickPos4 = '{"type":"SessionCommand","request_id":"pos-4","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition4"}}'
    $writer.WriteLine($clickPos4)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Position 4 clicked!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }
    Start-Sleep -Milliseconds 500

    # Click position 0 button second to assign AI player
    Write-Host "==> Clicking map start position 0..." -ForegroundColor Magenta
    $clickPos0 = '{"type":"SessionCommand","request_id":"pos-0","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition0"}}'
    $writer.WriteLine($clickPos0)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Position 0 clicked!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "1v1 setup complete!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green
    Write-Host "Player 0 (You): GLA at position 4" -ForegroundColor Cyan
    Write-Host "Player 1 (AI): China at position 0" -ForegroundColor Cyan
    Write-Host "`n"

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
