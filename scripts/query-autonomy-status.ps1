param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    $pipe.Connect(5000)

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 200

    # Query status
    $statusCmd = '{"type":"SessionCommand","request_id":"status-1","cmd":"Autonomy.Status"}'
    $writer.WriteLine($statusCmd)
    $resp = $reader.ReadLine()

    if ($resp) {
        $statusObj = ConvertFrom-Json $resp
        if ($statusObj.result) {
            Write-Host "Autonomy Status:" -ForegroundColor Cyan
            Write-Host "  mode: $($statusObj.result.mode)"
            Write-Host "  profile: $($statusObj.result.profile)"
            Write-Host "  paused: $($statusObj.result.paused)"
            Write-Host "  active: $($statusObj.result.active)"
            Write-Host "  debug_draw: $($statusObj.result.debug_draw)"
            Write-Host "  attack_enabled: $($statusObj.result.attack_enabled)"
            Write-Host "  capture_tech: $($statusObj.result.capture_tech)"
            Write-Host ""
            Write-Host "Full JSON:"
            $statusObj.result | ConvertTo-Json -Depth 10
        }
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
