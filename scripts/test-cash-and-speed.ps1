param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Starting Cash and Game Speed" -ForegroundColor Cyan

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    $pipe.Connect(5000)
    Write-Host "Connected!`n" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 1

    Write-Host "==> Testing Starting Cash combo box..." -ForegroundColor Magenta
    # Try different indices to find 50000
    # Typical cash values: 5000, 10000, 20000, 40000, 50000, 100000
    $cashControlId = "SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash"

    for ($i = 0; $i -le 6; $i++) {
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"cash-$i`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"$cashControlId`",`"index`":$i}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        $respObj = ConvertFrom-Json $resp

        if ($respObj.ok) {
            Write-Host "  Index $i - SUCCESS" -ForegroundColor Green
        } else {
            Write-Host "  Index $i - $($respObj.reason)" -ForegroundColor Red
            break
        }
        Start-Sleep -Milliseconds 500
    }

    Write-Host "`nLast selected index: Check screenshot to see cash amount" -ForegroundColor Yellow
    Write-Host "Typical indices: 0=5000, 1=10000, 2=20000, 3=40000, 4=50000, 5=100000" -ForegroundColor Gray

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
