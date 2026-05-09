param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "Connecting to named pipe: \\.\pipe\$PipeName..." -ForegroundColor Yellow
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

    # Query skirmish setup
    $query = '{"type":"SessionCommand","request_id":"query-1","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    Write-Host "Querying skirmish setup state...`n" -ForegroundColor Cyan
    $writer.WriteLine($query)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.result.available) {
        Write-Host "Skirmish Setup State:" -ForegroundColor Yellow
        Write-Host "  Map: $($respObj.result.map)" -ForegroundColor White
        Write-Host "  Starting Cash: $($respObj.result.starting_cash)" -ForegroundColor White
        Write-Host "  Superweapon Restricted: $($respObj.result.superweapon_restricted)" -ForegroundColor White
        Write-Host "  Seed: $($respObj.result.seed)" -ForegroundColor White
        Write-Host "  Local Slot: $($respObj.result.local_slot_num)" -ForegroundColor White
        Write-Host "  Is Host: $($respObj.result.is_host)`n" -ForegroundColor White

        Write-Host "Slots:" -ForegroundColor Yellow
        for ($i = 0; $i -lt $respObj.result.slots.Count; $i++) {
            $slot = $respObj.result.slots[$i]
            Write-Host "  Slot $i`: $($slot.state) | color: $($slot.color) | template: $($slot.template) | team: $($slot.team) | start_pos: $($slot.start_position) | name: '$($slot.name)'" -ForegroundColor White
        }
    } else {
        Write-Host "Skirmish not available" -ForegroundColor Red
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
    Write-Host "`nConnection closed." -ForegroundColor Yellow
}
