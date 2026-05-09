param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Death Valley 1v1 Complete Configuration" -ForegroundColor Cyan
Write-Host "Player: GLA | Opponent: Easy AI China" -ForegroundColor Cyan
Write-Host "Using UI-Driven Approach (Menu.SelectComboBox)" -ForegroundColor Cyan
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

    # Set Player faction to GLA (index 3)
    Write-Host "==> Setting Player faction to GLA..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"cfg-1","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate0","index":3}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    # Set Slot 1 to Easy AI
    Write-Host "==> Setting Slot 1 to Easy AI..." -ForegroundColor Magenta
    # Slot state: 0=Open, 1=Closed, 2=Easy, 3=Medium, 4=Hard
    $cmd = '{"type":"SessionCommand","request_id":"cfg-2","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayer1","index":2}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    # Set Slot 1 faction to China (index 2)
    Write-Host "==> Setting Slot 1 faction to China..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"cfg-3","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1","index":2}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    # Close remaining slots (2-7)
    Write-Host "==> Closing slots 2-7..." -ForegroundColor Magenta
    for ($i = 2; $i -le 7; $i++) {
        # Index 1 = Closed
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"close-$i`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ComboBoxPlayer$i`",`"index`":1}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        Start-Sleep -Milliseconds 200
    }
    Write-Host "    Slots closed!" -ForegroundColor Green

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Configuration Complete!" -ForegroundColor Green
    Write-Host "Player: GLA | Opponent: Easy AI China" -ForegroundColor Green
    Write-Host "Slots 2-7: Closed" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
