param(
    [string]$PipeName = "zh_ai_control",
    [string]$ButtonId = "SkirmishGameOptionsMenu.wnd:ButtonStart"
)

$ErrorActionPreference = "Stop"

Write-Host "Attempting to click Start/Play Game button..." -ForegroundColor Cyan
Write-Host "Button ID: $ButtonId" -ForegroundColor Yellow

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

    # Click the button
    $click = @{
        type = "SessionCommand"
        request_id = "click-start-1"
        cmd = "Menu.Click"
        args = @{
            controlId = $ButtonId
        }
    }

    $clickJson = ConvertTo-Json -InputObject $click -Compress
    Write-Host "Clicking button..." -ForegroundColor Gray
    $writer.WriteLine($clickJson)
    $resp = $reader.ReadLine()

    if ($null -eq $resp -or $resp -eq "") {
        Write-Host "No response received from adapter" -ForegroundColor Red
    } else {
        Write-Host "Response: $resp" -ForegroundColor Gray
        $respObj = ConvertFrom-Json $resp

        if ($respObj.ok) {
            Write-Host "Button clicked successfully!" -ForegroundColor Green
        } else {
            Write-Host "Failed to click: $($respObj.reason)" -ForegroundColor Red
        }
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
