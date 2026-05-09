param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Death Valley 1v1 - UI-Driven Setup" -ForegroundColor Cyan
Write-Host "Player: GLA | Opponent: China Easy AI" -ForegroundColor Cyan
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
    Start-Sleep -Seconds 1

    # Navigate to Skirmish Menu
    Write-Host "==> Navigating to Skirmish..." -ForegroundColor Magenta
    $clickSP = '{"type":"SessionCommand","request_id":"nav-1","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSinglePlayer"}}'
    $writer.WriteLine($clickSP)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    $clickSkirmish = '{"type":"SessionCommand","request_id":"nav-2","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSkirmish"}}'
    $writer.WriteLine($clickSkirmish)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 3
    Write-Host "    At skirmish menu`n" -ForegroundColor Green

    # Set Player faction to GLA (assuming index 3 based on typical ordering)
    Write-Host "==> Setting Player faction to GLA..." -ForegroundColor Magenta
    $selectGLA = '{"type":"SessionCommand","request_id":"sel-1","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1","index":3}}'
    $writer.WriteLine($selectGLA)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    if ($respObj.ok) {
        Write-Host "    Player faction set!" -ForegroundColor Green
    } else {
        Write-Host "    FAILED: $($respObj.reason)" -ForegroundColor Red
    }
    Start-Sleep -Seconds 1

    # Set AI opponent faction to China (index 2 confirmed)
    Write-Host "==> Setting AI faction to China..." -ForegroundColor Magenta
    $selectChina = '{"type":"SessionCommand","request_id":"sel-2","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate2","index":2}}'
    $writer.WriteLine($selectChina)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    if ($respObj.ok) {
        Write-Host "    AI faction set!" -ForegroundColor Green
    } else {
        Write-Host "    FAILED: $($respObj.reason)" -ForegroundColor Red
    }
    Start-Sleep -Seconds 1

    # Set AI player state to Easy AI
    Write-Host "==> Setting AI difficulty to Easy..." -ForegroundColor Magenta
    # Slot state indices: 0=Open, 1=Closed, 2=Easy, 3=Medium, 4=Hard (typical ordering)
    $selectEasy = '{"type":"SessionCommand","request_id":"sel-3","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayer2","index":2}}'
    $writer.WriteLine($selectEasy)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    if ($respObj.ok) {
        Write-Host "    AI difficulty set!" -ForegroundColor Green
    } else {
        Write-Host "    FAILED: $($respObj.reason)" -ForegroundColor Red
    }
    Start-Sleep -Seconds 1

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Configuration Complete!" -ForegroundColor Green
    Write-Host "Check game window - ready for screenshot" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
