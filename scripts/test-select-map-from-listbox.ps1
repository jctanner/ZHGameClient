param(
    [string]$PipeName = "zh_ai_control",
    [int]$MapIndex = 5  # Death Valley is often around index 5-10
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Test: Select Map from ListBox" -ForegroundColor Cyan
Write-Host "Map Index: $MapIndex" -ForegroundColor Cyan
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

    # Click "Select Map" button to open map selection menu
    Write-Host "==> Opening Map Selection Menu..." -ForegroundColor Magenta
    $clickSelectMap = '{"type":"SessionCommand","request_id":"nav-3","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonSelectMap"}}'
    $writer.WriteLine($clickSelectMap)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    # Select map from listbox using the new Menu.SelectListBox command
    Write-Host "==> Selecting map at index $MapIndex from listbox..." -ForegroundColor Magenta
    $selectMap = "{`"type`":`"SessionCommand`",`"request_id`":`"select-1`",`"cmd`":`"Menu.SelectListBox`",`"args`":{`"controlId`":`"SkirmishMapSelectMenu.wnd:ListboxMap`",`"index`":$MapIndex}}"
    $writer.WriteLine($selectMap)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Map selected!" -ForegroundColor Green
    } else {
        Write-Host "    Failed to select map: $($respObj.reason)" -ForegroundColor Red
    }
    Start-Sleep -Seconds 1

    # Click OK to confirm selection
    Write-Host "==> Clicking OK to confirm selection..." -ForegroundColor Magenta
    $clickOK = '{"type":"SessionCommand","request_id":"nav-4","cmd":"Menu.Click","args":{"controlId":"SkirmishMapSelectMenu.wnd:ButtonOK"}}'
    $writer.WriteLine($clickOK)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    OK clicked!" -ForegroundColor Green
    } else {
        Write-Host "    Failed to click OK: $($respObj.reason)" -ForegroundColor Red
    }

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Map selection complete - check the game UI!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
