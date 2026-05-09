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

    # Send Hello
    $hello = '{"type":"Hello","request_id":"hello-1"}'
    $writer.WriteLine($hello)
    $resp = $reader.ReadLine()
    Start-Sleep -Milliseconds 100

    # List controls
    $list = '{"type":"SessionCommand","request_id":"list-1","cmd":"Menu.ListControls"}'
    $writer.WriteLine($list)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "`nAvailable Controls:" -ForegroundColor Cyan
        foreach ($control in $respObj.result.controls) {
            Write-Host "  [$($control.controlId)] $($control.text)" -ForegroundColor White
        }
    } else {
        Write-Host "Failed to list controls" -ForegroundColor Red
    }

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
