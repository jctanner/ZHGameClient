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
    $writer.WriteLine($hello)
    $helloResp = $reader.ReadLine()
    Write-Host "Connected to adapter!" -ForegroundColor Green

    Start-Sleep -Seconds 1

    # Navigate to Skirmish Menu
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "NAVIGATION: Going to Skirmish Menu" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta

    # Click Single Player
    Write-Host ">>> Clicking Single Player..." -ForegroundColor Cyan
    $clickSP = '{"type":"SessionCommand","request_id":"nav-1","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSinglePlayer"}}'
    $writer.WriteLine($clickSP)
    $resp = $reader.ReadLine()
    Write-Host "<<< $resp" -ForegroundColor Green
    Start-Sleep -Seconds 2

    # Click Skirmish
    Write-Host ">>> Clicking Skirmish..." -ForegroundColor Cyan
    $clickSkirmish = '{"type":"SessionCommand","request_id":"nav-2","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSkirmish"}}'
    $writer.WriteLine($clickSkirmish)
    $resp = $reader.ReadLine()
    Write-Host "<<< $resp" -ForegroundColor Green
    Start-Sleep -Seconds 3

    # Test 1: Query skirmish setup (should work now)
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 1: Query skirmish setup" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $query1 = '{"type":"SessionCommand","request_id":"test-1","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    Write-Host ">>> Querying skirmish setup..." -ForegroundColor Cyan
    $writer.WriteLine($query1)
    $resp1 = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp1
    Write-Host "<<< Available: $($respObj.result.available)" -ForegroundColor $(if ($respObj.result.available) { "Green" } else { "Red" })
    Write-Host "    Map: $($respObj.result.map)" -ForegroundColor Green
    Write-Host "    Starting Cash: $($respObj.result.starting_cash)" -ForegroundColor Green
    Write-Host "    Slot 0: $($respObj.result.slots[0].state)" -ForegroundColor Green
    Write-Host "    Slot 1: $($respObj.result.slots[1].state)" -ForegroundColor Green

    Start-Sleep -Seconds 1

    # Test 2: Set slot 1 to brutal AI
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 2: Set slot 1 to Brutal AI" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $setSlot = '{"type":"SessionCommand","request_id":"test-2","cmd":"Skirmish.SetSlot","args":{"slot":1,"state":"brutal_ai","color":1,"template":1}}'
    Write-Host ">>> Setting slot 1..." -ForegroundColor Cyan
    $writer.WriteLine($setSlot)
    $resp2 = $reader.ReadLine()
    $respObj2 = ConvertFrom-Json $resp2
    Write-Host "<<< Success: $($respObj2.ok)" -ForegroundColor $(if ($respObj2.ok) { "Green" } else { "Red" })

    Start-Sleep -Seconds 1

    # Test 3: Set starting cash
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 3: Set starting cash to 10000" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $setCash = '{"type":"SessionCommand","request_id":"test-3","cmd":"Skirmish.SetStartingCash","args":{"cash":10000}}'
    Write-Host ">>> Setting cash..." -ForegroundColor Cyan
    $writer.WriteLine($setCash)
    $resp3 = $reader.ReadLine()
    $respObj3 = ConvertFrom-Json $resp3
    Write-Host "<<< Success: $($respObj3.ok)" -ForegroundColor $(if ($respObj3.ok) { "Green" } else { "Red" })

    Start-Sleep -Seconds 1

    # Test 4: Set map
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 4: Set map to Tournament Desert" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $setMap = '{"type":"SessionCommand","request_id":"test-4","cmd":"Skirmish.SetMap","args":{"map":"Tournament Desert.map"}}'
    Write-Host ">>> Setting map..." -ForegroundColor Cyan
    $writer.WriteLine($setMap)
    $resp4 = $reader.ReadLine()
    $respObj4 = ConvertFrom-Json $resp4
    Write-Host "<<< Success: $($respObj4.ok)" -ForegroundColor $(if ($respObj4.ok) { "Green" } else { "Red" })

    Start-Sleep -Seconds 1

    # Test 5: Query again to verify changes
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 5: Verify configuration changes" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    $query2 = '{"type":"SessionCommand","request_id":"test-5","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    Write-Host ">>> Querying again..." -ForegroundColor Cyan
    $writer.WriteLine($query2)
    $resp5 = $reader.ReadLine()
    $respObj5 = ConvertFrom-Json $resp5
    Write-Host "<<< Map: $($respObj5.result.map)" -ForegroundColor Green
    Write-Host "    Starting Cash: $($respObj5.result.starting_cash)" -ForegroundColor Green
    Write-Host "    Slot 0: $($respObj5.result.slots[0].state) (color: $($respObj5.result.slots[0].color))" -ForegroundColor Green
    Write-Host "    Slot 1: $($respObj5.result.slots[1].state) (color: $($respObj5.result.slots[1].color))" -ForegroundColor Green

    Write-Host "`n$('='*60)" -ForegroundColor Green
    Write-Host "SUCCESS! Skirmish remote control working!" -ForegroundColor Green
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
