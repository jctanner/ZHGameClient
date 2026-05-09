param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Player Faction Combo Box" -ForegroundColor Cyan

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

    # Try different player template combo box IDs for the player
    $playerTemplateIds = @(
        "SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate",
        "ComboBoxPlayerTemplate",
        "SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate0"
    )

    foreach ($cbId in $playerTemplateIds) {
        Write-Host "==> Trying $cbId (index 3 = GLA)..." -ForegroundColor Yellow
        $select = "{`"type`":`"SessionCommand`",`"request_id`":`"sel-test`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"$cbId`",`"index`":3}}"
        $writer.WriteLine($select)
        $resp = $reader.ReadLine()
        $respObj = ConvertFrom-Json $resp

        if ($respObj.ok) {
            Write-Host "    SUCCESS with $cbId!" -ForegroundColor Green
            break
        } else {
            Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        }
        Start-Sleep -Milliseconds 500
    }

    Write-Host "`nTest complete - ready for screenshot" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
