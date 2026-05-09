param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Starting Game (No Autonomy)..." -ForegroundColor Cyan

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
    Start-Sleep -Milliseconds 100

    # Just click the Start button (no autonomy, no Skirmish.Start)
    Write-Host "Clicking Start button to launch match..." -ForegroundColor Yellow
    $click = @{
        type = "SessionCommand"
        request_id = "click-1"
        cmd = "Menu.Click"
        args = @{
            controlId = "SkirmishGameOptionsMenu.wnd:ButtonStart"
        }
    }
    $writer.WriteLine((ConvertTo-Json -InputObject $click -Compress))

    # Try to read response, but don't fail if game transitions
    try {
        $resp = $reader.ReadLine()
        if ($null -ne $resp -and $resp -ne "") {
            Write-Host "Response: $resp" -ForegroundColor Gray
        }
    } catch {
        Write-Host "Game transitioned (connection closed)" -ForegroundColor Yellow
    }

    Write-Host "Start button clicked!" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
