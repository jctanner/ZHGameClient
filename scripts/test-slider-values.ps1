param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Game Speed Slider Values" -ForegroundColor Cyan

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

    Write-Host "`nTesting different slider values...`n" -ForegroundColor Magenta

    foreach ($val in @(30, 60, 61, 70, 80, 90, 100, 120, 150, 200, 255)) {
        Write-Host "Setting slider to $val..." -ForegroundColor Cyan
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"slider-$val`",`"cmd`":`"Menu.SetSlider`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:SliderGameSpeed`",`"value`":$val}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        $respObj = ConvertFrom-Json $resp

        if ($respObj.ok) {
            Write-Host "  Success" -ForegroundColor Green
        } else {
            Write-Host "  Failed: $($respObj.reason)" -ForegroundColor Red
        }

        Start-Sleep -Milliseconds 500
    }

    Write-Host "`nTake screenshot to see final value" -ForegroundColor Cyan

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
