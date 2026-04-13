param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Verifying Configuration" -ForegroundColor Cyan

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

    # Query skirmish setup
    Write-Host "==> Querying internal game state..." -ForegroundColor Magenta
    $query = '{"type":"SessionCommand","request_id":"query-1","cmd":"Game.Query","args":{"path":"game.skirmish_setup"}}'
    $writer.WriteLine($query)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        $result = $respObj.result

        Write-Host "`nInternal Game State:" -ForegroundColor Yellow
        Write-Host "  Map: $($result.map)" -ForegroundColor White
        Write-Host "  Starting Cash: $($result.starting_cash)" -ForegroundColor White
        Write-Host "  Superweapon Restricted: $($result.superweapon_restricted)" -ForegroundColor White
        Write-Host ""

        Write-Host "Slots:" -ForegroundColor Yellow
        for ($i = 0; $i -lt 2; $i++) {
            $slot = $result.slots[$i]
            Write-Host "  Slot $i - state=$($slot.state) faction_template=$($slot.template) color=$($slot.color) start_pos=$($slot.start_position) name='$($slot.name)'" -ForegroundColor White
        }
        Write-Host ""
    } else {
        Write-Host "Query failed: $($respObj.reason)" -ForegroundColor Red
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
