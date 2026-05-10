param(
    [string]$PipeName = "zh_ai_control",
    [bool]$Enabled = $true
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Toggle Debug Zone Drawing" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

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
    Start-Sleep -Milliseconds 200

    # Configure debug drawing
    $status = if ($Enabled) { "enabling" } else { "disabling" }
    Write-Host "==> $status debug zone drawing..." -ForegroundColor Magenta

    $enabledJson = if ($Enabled) { "true" } else { "false" }
    $configCmd = "{`"type`":`"SessionCommand`",`"request_id`":`"debug-1`",`"cmd`":`"Autonomy.Configure`",`"args`":{`"debug_draw`":$enabledJson}}"
    $writer.WriteLine($configCmd)
    $resp = $reader.ReadLine()

    if (-not $resp) {
        Write-Host "    Failed: No response from adapter (pipe disconnected?)" -ForegroundColor Red
        exit 1
    }

    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        $statusWord = if ($Enabled) { "enabled" } else { "disabled" }
        Write-Host "    Debug zone drawing $statusWord!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }

    # Query status to confirm
    Write-Host "`n==> Querying autonomy status..." -ForegroundColor Magenta
    $statusCmd = '{"type":"SessionCommand","request_id":"status-1","cmd":"Autonomy.Status"}'
    $writer.WriteLine($statusCmd)
    $resp = $reader.ReadLine()

    if ($resp) {
        $statusObj = ConvertFrom-Json $resp
        if ($statusObj.result) {
            Write-Host "    Current debug_draw: $($statusObj.result.debug_draw)" -ForegroundColor Cyan
        }
    }

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Done!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

    if ($Enabled) {
        Write-Host "Zone drawing enabled!" -ForegroundColor Yellow
        Write-Host "  • Green circles = main base zone" -ForegroundColor White
        Write-Host "  • Cyan circles = expansion zones" -ForegroundColor White
        Write-Host "  • Crosshairs mark zone centers" -ForegroundColor White
        Write-Host "`n"
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
