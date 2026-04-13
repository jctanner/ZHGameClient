param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Finding ComboBoxStartingCash Index for 50000" -ForegroundColor Cyan

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

    Write-Host "`nTesting cash combo box indices...`n" -ForegroundColor Magenta

    for ($i = 0; $i -le 10; $i++) {
        # Select index
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"cash-$i`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash`",`"index`":$i}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        $respObj = ConvertFrom-Json $resp

        if (-not $respObj.ok) {
            Write-Host "Index $i - Out of range" -ForegroundColor Yellow
            break
        }

        Start-Sleep -Milliseconds 300

        # Query state
        $query = '{"type":"SessionCommand","request_id":"query","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
        $writer.WriteLine($query)
        $resp = $reader.ReadLine()
        $queryObj = ConvertFrom-Json $resp

        if ($queryObj.ok) {
            $cash = $queryObj.result.starting_cash
            Write-Host "Index $i -> Cash $cash" -ForegroundColor $(if ($cash -eq 50000) { "Green" } else { "White" })
        }

        Start-Sleep -Milliseconds 200
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
