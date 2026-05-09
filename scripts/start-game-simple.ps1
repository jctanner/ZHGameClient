param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Starting Game..." -ForegroundColor Cyan

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

    # Just click the Start button
    Write-Host "Clicking Start button..." -ForegroundColor Yellow
    $click = @{
        type = "SessionCommand"
        request_id = "click-1"
        cmd = "Menu.Click"
        args = @{
            controlId = "SkirmishGameOptionsMenu.wnd:ButtonStart"
        }
    }
    $writer.WriteLine((ConvertTo-Json -InputObject $click -Compress))
    $resp = $reader.ReadLine()

    if ($null -ne $resp -and $resp -ne "") {
        Write-Host "Response: $resp" -ForegroundColor Gray
        $respObj = ConvertFrom-Json $resp
        if ($respObj.ok) {
            Write-Host "Game starting!" -ForegroundColor Green
        } else {
            Write-Host "Failed: $($respObj.reason)" -ForegroundColor Red
        }
    } else {
        Write-Host "No response received" -ForegroundColor Yellow
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
