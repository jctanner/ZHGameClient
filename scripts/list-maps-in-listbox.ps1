param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "List All Maps in ListBox" -ForegroundColor Cyan
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

    Write-Host "`n==> Trying different indices to find Death Valley..." -ForegroundColor Cyan
    Write-Host ""

    # Try indices 0-20
    for ($i = 0; $i -le 20; $i++) {
        $selectMap = "{`"type`":`"SessionCommand`",`"request_id`":`"select-$i`",`"cmd`":`"Menu.SelectListBox`",`"args`":{`"controlId`":`"SkirmishMapSelectMenu.wnd:ListboxMap`",`"index`":$i}}"
        $writer.WriteLine($selectMap)
        $resp = $reader.ReadLine()

        Start-Sleep -Milliseconds 200

        # The map name should appear somewhere in the UI
        # For now, just report that we selected index $i
        Write-Host "  Index $i selected (check game UI for map name)" -ForegroundColor Gray
    }

    Write-Host "`n========================================" -ForegroundColor Yellow
    Write-Host "Check the game UI to see which index shows Death Valley!" -ForegroundColor Yellow
    Write-Host "Then update the test script with the correct index." -ForegroundColor Yellow
    Write-Host "========================================`n" -ForegroundColor Yellow

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
