param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Death Valley 1v1 - UI-Driven Configuration" -ForegroundColor Cyan
Write-Host "Human GLA (Top Left) vs Easy AI China (Bottom Right)" -ForegroundColor Cyan
Write-Host "Using Menu.SelectComboBox to trigger proper callbacks" -ForegroundColor Cyan
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
    $clickSP = '{"type":"SessionCommand","request_id":"nav-1","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSinglePlayer"}}'
    $writer.WriteLine($clickSP)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    $clickSkirmish = '{"type":"SessionCommand","request_id":"nav-2","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSkirmish"}}'
    $writer.WriteLine($clickSkirmish)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 3
    Write-Host "    At skirmish menu`n" -ForegroundColor Green

    # List all combo box controls to see what's available
    Write-Host "==> Finding combo box controls..." -ForegroundColor Magenta
    $listControls = '{"type":"SessionCommand","request_id":"list-1","cmd":"Menu.ListControls","args":{"include_hidden":false}}'
    $writer.WriteLine($listControls)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    $comboBoxes = $respObj.result.controls | Where-Object { $_.type -eq "combo_box" }
    Write-Host "Combo boxes found:" -ForegroundColor Yellow
    foreach ($cb in $comboBoxes) {
        Write-Host "  $($cb.controlId)" -ForegroundColor White
    }
    Write-Host ""

    # Try to select player faction (GLA = index 2)
    Write-Host "==> Selecting player faction to GLA (index 2)..." -ForegroundColor Magenta
    $selectFaction = '{"type":"SessionCommand","request_id":"select-1","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1","index":2}}'
    $writer.WriteLine($selectFaction)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result: $($respObj.ok) - $($respObj.reason)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    # Try to select AI faction (China = index 1)
    Write-Host "==> Selecting AI faction to China (index 1)..." -ForegroundColor Magenta
    $selectAIFaction = '{"type":"SessionCommand","request_id":"select-2","cmd":"Menu.SelectComboBox","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate2","index":1}}'
    $writer.WriteLine($selectAIFaction)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Result: $($respObj.ok) - $($respObj.reason)" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })
    Start-Sleep -Seconds 1

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "UI-driven configuration attempt complete!" -ForegroundColor Green
    Write-Host "Check game window to see if factions changed" -ForegroundColor Green
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
