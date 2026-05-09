param(
    [string]$PipeName = "zh_ai_control",
    [string]$MapName = "Death Valley.map"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Test: Select Map via UI" -ForegroundColor Cyan
Write-Host "Map: $MapName" -ForegroundColor Cyan
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

    # Click "Select Map" button to open map selection menu (this populates the cache)
    Write-Host "==> Opening Map Selection Menu (populates cache)..." -ForegroundColor Magenta
    $clickSelectMap = '{"type":"SessionCommand","request_id":"nav-3","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonSelectMap"}}'
    $writer.WriteLine($clickSelectMap)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Map selection menu opened!" -ForegroundColor Green
    } else {
        Write-Host "    Failed to open map menu: $($respObj.reason)" -ForegroundColor Red
    }
    Start-Sleep -Seconds 2

    # Close the map selection menu by clicking Back
    Write-Host "==> Closing map selection menu..." -ForegroundColor Magenta
    $clickBack = '{"type":"SessionCommand","request_id":"nav-4","cmd":"Menu.Click","args":{"controlId":"SkirmishMapSelectMenu.wnd:ButtonBack"}}'
    $writer.WriteLine($clickBack)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Map selection menu closed!" -ForegroundColor Green
    } else {
        Write-Host "    Failed to close map menu: $($respObj.reason)" -ForegroundColor Red
    }
    Start-Sleep -Seconds 1

    # Now try to set the map programmatically (cache should be populated now)
    Write-Host "==> Setting map to $MapName via API..." -ForegroundColor Magenta
    $setMap = "{`"type`":`"SessionCommand`",`"request_id`":`"cfg-1`",`"cmd`":`"Skirmish.SetMap`",`"args`":{`"map`":`"$MapName`"}}"
    $writer.WriteLine($setMap)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Map set successfully!" -ForegroundColor Green
    } else {
        Write-Host "    Map set failed: $($respObj.reason)" -ForegroundColor Red
    }

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Test Complete!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
