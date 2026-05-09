param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`nDiagnosing skirmish configuration issue..." -ForegroundColor Cyan
Write-Host "=========================================`n" -ForegroundColor Cyan

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

    # Query skirmish state
    Write-Host "==> Querying current skirmish state..." -ForegroundColor Yellow
    $query = '{"type":"SessionCommand","request_id":"query-1","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    $writer.WriteLine($query)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        $result = $respObj.result

        Write-Host "Skirmish Setup:" -ForegroundColor Green
        Write-Host "  Available: $($result.available)" -ForegroundColor White
        Write-Host "  Map: $($result.map)" -ForegroundColor White
        Write-Host "  Starting Cash: $($result.starting_cash)" -ForegroundColor White
        Write-Host "  Superweapon Restricted: $($result.superweapon_restricted)" -ForegroundColor White
        Write-Host "  Local Slot: $($result.local_slot_num)" -ForegroundColor White
        Write-Host "  Is Host: $($result.is_host)" -ForegroundColor White
        Write-Host ""

        Write-Host "Player Slots:" -ForegroundColor Green
        for ($i = 0; $i -lt $result.slots.Count; $i++) {
            $slot = $result.slots[$i]
            if ($slot.state -ne "closed") {
                Write-Host "  Slot $i  :" -ForegroundColor Cyan -NoNewline
                Write-Host " state=$($slot.state)" -ForegroundColor White -NoNewline
                Write-Host " template=$($slot.template)" -ForegroundColor White -NoNewline
                Write-Host " color=$($slot.color)" -ForegroundColor White -NoNewline
                Write-Host " start_pos=$($slot.start_position)" -ForegroundColor White -NoNewline
                Write-Host " name='$($slot.name)'" -ForegroundColor White
            }
        }
        Write-Host ""
    } else {
        Write-Host "Query failed: $($respObj.reason)" -ForegroundColor Red
    }

    # List available controls to see if slots are visible
    Write-Host "==> Listing visible controls..." -ForegroundColor Yellow
    $listControls = '{"type":"SessionCommand","request_id":"list-1","cmd":"Menu.ListControls","args":{"include_hidden":false}}'
    $writer.WriteLine($listControls)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        $playerControls = $respObj.result.controls | Where-Object {
            $_.controlId -like "*ComboBoxPlayer*" -or
            $_.controlId -like "*ComboBoxColor*" -or
            $_.controlId -like "*ComboBoxPlayerTemplate*"
        }

        Write-Host "Player-related controls (visible only):" -ForegroundColor Green
        if ($playerControls) {
            foreach ($ctrl in $playerControls) {
                Write-Host "  $($ctrl.controlId)" -ForegroundColor White -NoNewline
                Write-Host " - type: $($ctrl.type)" -ForegroundColor Gray -NoNewline
                Write-Host " - enabled: $($ctrl.enabled)" -ForegroundColor Gray
            }
        } else {
            Write-Host "  NO PLAYER CONTROLS VISIBLE!" -ForegroundColor Red
        }
        Write-Host ""
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
