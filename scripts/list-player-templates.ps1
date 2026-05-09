param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`nQuerying player templates..." -ForegroundColor Cyan

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

    # Query capabilities to see if there's a template query command
    Write-Host "Capabilities:" -ForegroundColor Yellow
    $helloObj = ConvertFrom-Json $helloResp
    $helloObj.capabilities | ForEach-Object { Write-Host "  $_" -ForegroundColor White }
    Write-Host ""

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
