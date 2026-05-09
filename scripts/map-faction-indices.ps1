param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Mapping Faction Combo Box Indices to Template IDs" -ForegroundColor Cyan

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

    Write-Host "Testing Slot 1 faction combo box indices..." -ForegroundColor Magenta
    Write-Host "Format: Index -> Template ID`n" -ForegroundColor Gray

    for ($i = 0; $i -le 15; $i++) {
        # Set the combo box
        $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"set-$i`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate1`",`"index`":$i}}"
        $writer.WriteLine($cmd)
        $resp = $reader.ReadLine()
        $respObj = ConvertFrom-Json $resp

        if (-not $respObj.ok) {
            Write-Host "Index $i - Out of range (max index is $($i-1))" -ForegroundColor Yellow
            break
        }

        Start-Sleep -Milliseconds 300

        # Query state
        $query = '{"type":"SessionCommand","request_id":"query","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
        $writer.WriteLine($query)
        $resp = $reader.ReadLine()
        $queryObj = ConvertFrom-Json $resp

        if ($queryObj.ok) {
            $template = $queryObj.result.slots[1].template
            Write-Host "Index $i -> Template $template" -ForegroundColor White
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
