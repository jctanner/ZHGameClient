param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    $pipe.Connect(5000)
    Write-Host "Connected!" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 100

    # Status
    $status = '{"type":"SessionCommand","request_id":"status-1","cmd":"Autonomy.Status"}'
    $writer.WriteLine($status)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    Write-Host "Mode: $($respObj.result.mode)" -ForegroundColor Cyan
    Write-Host "Active: $($respObj.result.active)" -ForegroundColor Cyan
    Write-Host "Paused: $($respObj.result.paused)" -ForegroundColor Cyan
    Write-Host "Profile: $($respObj.result.profile)" -ForegroundColor Cyan
    Write-Host "Money: $($respObj.result.money)" -ForegroundColor Cyan
    Write-Host "Workers: $($respObj.result.assets.workers)" -ForegroundColor Cyan
    Write-Host "Buildings: $($respObj.result.assets.buildings)" -ForegroundColor Cyan
    Write-Host "Last Decision: $($respObj.result.last_decision.category) - $($respObj.result.last_decision.command) ($($respObj.result.last_decision.reason))" -ForegroundColor Yellow

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
