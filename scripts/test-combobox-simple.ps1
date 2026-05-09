param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Menu.SelectComboBox command" -ForegroundColor Cyan

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
    $helloResp = $reader.ReadLine()
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

    # Try different player template combo box IDs
    $comboBoxIds = @(
        "SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1",
        "SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate2",
        "ComboBoxPlayerTemplate1",
        "ComboBoxPlayerTemplate"
    )

    foreach ($cbId in $comboBoxIds) {
        Write-Host "==> Trying to select $cbId index 2..." -ForegroundColor Yellow
        $select = "{`"type`":`"SessionCommand`",`"request_id`":`"sel-1`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"$cbId`",`"index`":2}}"
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

    Write-Host "`n==> Test complete" -ForegroundColor Cyan

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
