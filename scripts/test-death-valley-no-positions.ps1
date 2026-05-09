param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Death Valley 1v1 Skirmish Test (NO POSITIONS)" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

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
    $writer.WriteLine($hello)
    $helloResp = $reader.ReadLine()
    Write-Host "Adapter connected!`n" -ForegroundColor Green
    Start-Sleep -Seconds 1

    # Navigate to Skirmish Menu
    Write-Host "==> Navigating to Skirmish Menu..." -ForegroundColor Magenta

    # Click Single Player
    Write-Host "    Clicking Single Player..." -ForegroundColor Cyan
    $clickSP = '{"type":"SessionCommand","request_id":"nav-1","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSinglePlayer"}}'
    $writer.WriteLine($clickSP)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    # Click Skirmish
    Write-Host "    Clicking Skirmish..." -ForegroundColor Cyan
    $clickSkirmish = '{"type":"SessionCommand","request_id":"nav-2","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSkirmish"}}'
    $writer.WriteLine($clickSkirmish)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 3
    Write-Host "    Ready!`n" -ForegroundColor Green

    # Configure Death Valley 1v1
    Write-Host "==> Configuring Death Valley 1v1 (NO START POSITIONS)..." -ForegroundColor Magenta

    # Set map to Death Valley
    Write-Host "    Setting map: Death Valley..." -ForegroundColor Cyan
    $setMap = '{"type":"SessionCommand","request_id":"cfg-1","cmd":"Skirmish.SetMap","args":{"map":"Death Valley.map"}}'
    $writer.WriteLine($setMap)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    if ($respObj.ok) {
        Write-Host "    Map set successfully!" -ForegroundColor Green
    }
    Start-Sleep -Seconds 1

    # Configure Slot 0 (Human GLA) - NO START POSITION
    Write-Host "    Setting Slot 0: Human GLA (no position)..." -ForegroundColor Cyan
    $setSlot0 = '{"type":"SessionCommand","request_id":"cfg-2","cmd":"Skirmish.SetSlot","args":{"slot":0,"state":"human","color":0,"template":2,"name":"Player"}}'
    $writer.WriteLine($setSlot0)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Slot 0: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    # Configure Slot 1 (Easy AI China) - NO START POSITION
    Write-Host "    Setting Slot 1: Easy AI China (no position)..." -ForegroundColor Cyan
    $setSlot1 = '{"type":"SessionCommand","request_id":"cfg-3","cmd":"Skirmish.SetSlot","args":{"slot":1,"state":"easy_ai","color":1,"template":1}}'
    $writer.WriteLine($setSlot1)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Slot 1: $($respObj.ok)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    # Close remaining slots
    Write-Host "    Closing remaining slots (2-7)..." -ForegroundColor Cyan
    for ($i = 2; $i -le 7; $i++) {
        $closeSlot = "{`"type`":`"SessionCommand`",`"request_id`":`"cfg-close-$i`",`"cmd`":`"Skirmish.SetSlot`",`"args`":{`"slot`":$i,`"state`":`"closed`"}}"
        $writer.WriteLine($closeSlot)
        $resp = $reader.ReadLine()
    }
    Write-Host "    Slots closed!`n" -ForegroundColor Green
    Start-Sleep -Seconds 1

    # Set starting cash
    Write-Host "    Setting starting cash: 50000..." -ForegroundColor Cyan
    $setCash = '{"type":"SessionCommand","request_id":"cfg-4","cmd":"Skirmish.SetStartingCash","args":{"cash":50000}}'
    $writer.WriteLine($setCash)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Cash: $($respObj.ok)`n" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    # Refresh UI
    Write-Host "    Refreshing UI..." -ForegroundColor Cyan
    $refreshUI = '{"type":"SessionCommand","request_id":"cfg-5","cmd":"Skirmish.RefreshUI"}'
    $writer.WriteLine($refreshUI)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    UI Refreshed: $($respObj.ok)`n" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Configuration Complete (NO POSITIONS SET)!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
    Write-Host "Connection closed." -ForegroundColor Yellow
}
