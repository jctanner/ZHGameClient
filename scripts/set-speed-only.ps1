param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Setting Game Speed to Maximum" -ForegroundColor Cyan

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

    Write-Host "Setting slider to 255..." -ForegroundColor Magenta
    $cmd = '{"type":"SessionCommand","request_id":"speed","cmd":"Menu.SetSlider","args":{"controlId":"SkirmishGameOptionsMenu.wnd:SliderGameSpeed","value":255}}'
    $writer.WriteLine($cmd)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "Success - check if slider is visually at max" -ForegroundColor Green
    } else {
        Write-Host "Failed: $($respObj.reason)" -ForegroundColor Red
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
