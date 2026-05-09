param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "Sending Autonomy.Resume command..." -ForegroundColor Cyan

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    Write-Host "Connecting..."
    $pipe.Connect(5000)
    Write-Host "Connected!" -ForegroundColor Green

    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    # Send Resume command
    $resume = @{
        type = "SessionCommand"
        request_id = "resume-1"
        cmd = "Autonomy.Resume"
        args = @{}
    }

    Write-Host "Sending Autonomy.Resume..." -ForegroundColor Yellow
    $writer.WriteLine((ConvertTo-Json -InputObject $resume -Compress))
    $resp = $reader.ReadLine()

    Write-Host "Response: $resp" -ForegroundColor Green

    if ($resp) {
        $respObj = ConvertFrom-Json $resp
        if ($respObj.ok) {
            Write-Host ""
            Write-Host "*** AUTONOMY RESUMED ***" -ForegroundColor Green
        } else {
            Write-Host "Resume failed: $($respObj.reason)" -ForegroundColor Red
        }
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
