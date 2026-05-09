param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Death Valley 1v1 - GUI Map Selection" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

Write-Host "Connecting to named pipe: \\.\pipe\$PipeName..." -ForegroundColor Yellow
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
    $helloResp = $reader.ReadLine()
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

    # List available controls to find map selector
    Write-Host "==> Finding map selection controls..." -ForegroundColor Magenta
    $listControls = '{"type":"SessionCommand","request_id":"list-1","cmd":"Menu.ListControls","args":{"include_hidden":false}}'
    $writer.WriteLine($listControls)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    $mapControls = $respObj.result.controls | Where-Object { $_.controlId -like "*Map*" -or $_.controlId -like "*map*" }
    Write-Host "Map-related controls found:" -ForegroundColor Yellow
    foreach ($ctrl in $mapControls) {
        Write-Host "  $($ctrl.controlId) - $($ctrl.type) - hidden:$($ctrl.hidden) enabled:$($ctrl.enabled)" -ForegroundColor White
    }
    Write-Host ""

    Write-Host "Approach: We'll configure slots first, then you can manually select the map" -ForegroundColor Yellow
    Write-Host ""

    # Configure slots programmatically
    Write-Host "==> Configuring player slots..." -ForegroundColor Magenta

    $setSlot0 = '{"type":"SessionCommand","request_id":"cfg-1","cmd":"Skirmish.SetSlot","args":{"slot":0,"state":"human","color":0,"template":0,"start_position":0}}'
    $writer.WriteLine($setSlot0)
    $resp = $reader.ReadLine()
    Write-Host "    Slot 0: Human (top left)" -ForegroundColor Green

    $setSlot1 = '{"type":"SessionCommand","request_id":"cfg-2","cmd":"Skirmish.SetSlot","args":{"slot":1,"state":"easy_ai","color":1,"template":1,"start_position":7}}'
    $writer.WriteLine($setSlot1)
    $resp = $reader.ReadLine()
    Write-Host "    Slot 1: Easy AI (bottom right)" -ForegroundColor Green

    for ($i = 2; $i -le 7; $i++) {
        $closeSlot = "{`"type`":`"SessionCommand`",`"request_id`":`"cfg-close-$i`",`"cmd`":`"Skirmish.SetSlot`",`"args`":{`"slot`":$i,`"state`":`"closed`"}}"
        $writer.WriteLine($closeSlot)
        $resp = $reader.ReadLine()
    }
    Write-Host "    Slots 2-7: Closed" -ForegroundColor Green

    $setCash = '{"type":"SessionCommand","request_id":"cfg-3","cmd":"Skirmish.SetStartingCash","args":{"cash":10000}}'
    $writer.WriteLine($setCash)
    $resp = $reader.ReadLine()
    Write-Host "    Starting cash: 10000`n" -ForegroundColor Green

    $refreshUI = '{"type":"SessionCommand","request_id":"cfg-4","cmd":"Skirmish.RefreshUI"}'
    $writer.WriteLine($refreshUI)
    $resp = $reader.ReadLine()
    Write-Host "    UI Refreshed" -ForegroundColor Green

    Write-Host "`n========================================" -ForegroundColor Yellow
    Write-Host "NEXT STEP: Manually select Death Valley map in the UI" -ForegroundColor Yellow
    Write-Host "Then click Start to begin the game" -ForegroundColor Yellow
    Write-Host "========================================`n" -ForegroundColor Yellow

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
    Write-Host "Connection closed." -ForegroundColor Yellow
}
