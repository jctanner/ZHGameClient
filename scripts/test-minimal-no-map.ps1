param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Minimal Skirmish Test (NO MAP CHANGE)" -ForegroundColor Cyan
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

    # DON'T change map, DON'T set positions - just set starting cash
    Write-Host "==> Setting starting cash only..." -ForegroundColor Magenta

    # Set starting cash
    Write-Host "    Setting starting cash: 50000..." -ForegroundColor Cyan
    $setCash = '{"type":"SessionCommand","request_id":"cfg-1","cmd":"Skirmish.SetStartingCash","args":{"cash":50000}}'
    $writer.WriteLine($setCash)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp
    Write-Host "    Cash: $($respObj.ok)`n" -ForegroundColor $(if ($respObj.ok) { "Green" } else { "Red" })

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Configuration Complete (MINIMAL)!" -ForegroundColor Green
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
