param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Death Valley 1v1 - Complete Configuration" -ForegroundColor Cyan
Write-Host "Player: GLA @ Top Left | AI: Easy China @ Bottom Right" -ForegroundColor Cyan
Write-Host "Cash: 50000 | Speed: Maximum" -ForegroundColor Cyan
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

    # UI-Driven: Set Player faction to GLA
    Write-Host "==> Setting Player faction to GLA..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"cfg-1","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate0","index":3}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Write-Host "    Player faction: GLA" -ForegroundColor Green
    Start-Sleep -Seconds 1

    # UI-Driven: Set Slot 1 to Easy AI
    Write-Host "==> Setting Slot 1 to Easy AI..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"cfg-2","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayer1","index":2}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Write-Host "    Slot 1 difficulty: Easy" -ForegroundColor Green
    Start-Sleep -Seconds 1

    # UI-Driven: Set Slot 1 faction to China
    Write-Host "==> Setting Slot 1 faction to China..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"cfg-3","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1","index":2}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Write-Host "    Slot 1 faction: China" -ForegroundColor Green
    Start-Sleep -Seconds 1

    # UI-Driven: Close slots 2-7
    Write-Host "==> Closing slots 2-7..." -ForegroundColor Magenta
    for ($i = 2; $i -le 7; $i++) {
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"close-$i`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ComboBoxPlayer$i`",`"index`":1}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        Start-Sleep -Milliseconds 200
    }
    Write-Host "    Slots closed" -ForegroundColor Green

    # UI-Driven: Set starting positions via button clicks
    Write-Host "==> Setting start positions..." -ForegroundColor Magenta
    # Click position 4 button (top left) - places player there
    $cmd = '{"type":"SessionCommand","request_id":"cfg-pos-4","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition4"}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Write-Host "    Player position: 4 (top left)" -ForegroundColor Green
    Start-Sleep -Milliseconds 500

    # Click position 0 button (bottom right) - places AI there
    $cmd = '{"type":"SessionCommand","request_id":"cfg-pos-0","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonMapStartPosition0"}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    Write-Host "    AI position: 0 (bottom right)" -ForegroundColor Green
    Start-Sleep -Milliseconds 500

    # UI-Driven: Set starting cash to 50000 (index 3)
    Write-Host "==> Setting starting cash to 50000..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"cfg-cash","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash","index":3}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    if ($respObj.ok) {
        Write-Host "    Starting cash: 50000" -ForegroundColor Green
    } else {
        Write-Host "    Cash setting failed: $($respObj.reason)" -ForegroundColor Yellow
    }
    Start-Sleep -Milliseconds 500

    # UI-Driven: Set game speed to maximum (slider max is 60)
    Write-Host "==> Setting game speed to maximum..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"cfg-speed","cmd":"Menu.SetSlider","args":{"controlId":"SkirmishGameOptionsMenu.wnd:SliderGameSpeed","value":60}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    if ($respObj.ok) {
        Write-Host "    Game speed: Maximum (unlimited FPS)" -ForegroundColor Green
    } else {
        Write-Host "    Speed setting failed: $($respObj.reason)" -ForegroundColor Yellow
    }
    Start-Sleep -Milliseconds 500

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Configuration Complete!" -ForegroundColor Green
    Write-Host "Player: GLA @ Top Left" -ForegroundColor White
    Write-Host "Opponent: Easy AI China @ Bottom Right" -ForegroundColor White
    Write-Host "Cash: 50000 | Speed: Maximum" -ForegroundColor White
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
