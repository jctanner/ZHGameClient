param(
    [string]$PipeName = "zh_ai_control",
    [int]$SprawlMultiplier = 10
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Manually Enable Autonomy" -ForegroundColor Cyan
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
    Write-Host "Hello response: $resp`n" -ForegroundColor Gray
    Start-Sleep -Milliseconds 500

    # Check current autonomy status
    Write-Host "==> Checking autonomy status..." -ForegroundColor Magenta
    $status = '{"type":"SessionCommand","request_id":"status-1","cmd":"Autonomy.Status"}'
    $writer.WriteLine($status)
    $resp = $reader.ReadLine()
    Write-Host "Status response: $resp`n" -ForegroundColor Gray
    Start-Sleep -Milliseconds 500

    # Configure autonomy with sprawl profile and multiplier
    Write-Host "==> Configuring autonomy (sprawl profile, ${SprawlMultiplier}x multiplier)..." -ForegroundColor Magenta
    $configAutonomy = "{`"type`":`"SessionCommand`",`"request_id`":`"config-1`",`"cmd`":`"Autonomy.Configure`",`"args`":{`"profile`":`"sprawl`",`"sprawl_multiplier`":$SprawlMultiplier}}"
    Write-Host "Sending: $configAutonomy" -ForegroundColor Gray
    $writer.WriteLine($configAutonomy)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Autonomy configured!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        Write-Host "    Response: $resp" -ForegroundColor Gray
    }
    Start-Sleep -Milliseconds 500

    # Enable autonomy mode (autonomous = full autonomy)
    Write-Host "`n==> Enabling autonomous mode..." -ForegroundColor Magenta
    $setMode = '{"type":"SessionCommand","request_id":"mode-1","cmd":"Autonomy.SetMode","args":{"mode":"autonomous"}}'
    Write-Host "Sending: $setMode" -ForegroundColor Gray
    $writer.WriteLine($setMode)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Autonomy mode enabled!" -ForegroundColor Green
    } else {
        Write-Host "    Failed: $($respObj.reason)" -ForegroundColor Red
        Write-Host "    Response: $resp" -ForegroundColor Gray
    }
    Start-Sleep -Milliseconds 500

    # Check status again
    Write-Host "`n==> Checking autonomy status again..." -ForegroundColor Magenta
    $status2 = '{"type":"SessionCommand","request_id":"status-2","cmd":"Autonomy.Status"}'
    $writer.WriteLine($status2)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    Write-Host "`nFinal Status:" -ForegroundColor Yellow
    Write-Host "  Mode: $($respObj.result.mode)" -ForegroundColor White
    Write-Host "  Profile: $($respObj.result.profile)" -ForegroundColor White
    Write-Host "  Paused: $($respObj.result.paused)" -ForegroundColor White
    Write-Host "  Sprawl Multiplier: $($respObj.result.sprawl_multiplier)" -ForegroundColor White

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Autonomy enabled!" -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
