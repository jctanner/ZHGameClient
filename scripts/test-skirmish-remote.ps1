param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

# Helper to send JSON command and wait for response
function Send-AdapterCommand {
    param(
        [System.IO.Pipes.NamedPipeClientStream]$Pipe,
        [string]$Command,
        [hashtable]$Args = @{}
    )

    $requestId = "test-$(Get-Date -Format 'yyyyMMddHHmmssfff')"
    $message = @{
        type = "SessionCommand"
        request_id = $requestId
        cmd = $Command
    }

    if ($Args.Count -gt 0) {
        $message.args = $Args
    }

    $json = ConvertTo-Json $message -Compress -Depth 10
    Write-Host "`n>>> Sending: $Command" -ForegroundColor Cyan
    Write-Host "    $json"

    $writer = [System.IO.StreamWriter]::new($Pipe)
    $writer.AutoFlush = $true
    $writer.WriteLine($json)

    # Read response
    $reader = [System.IO.StreamReader]::new($Pipe)
    $response = $reader.ReadLine()

    if ($response) {
        $responseObj = ConvertFrom-Json $response
        Write-Host "<<< Response:" -ForegroundColor Green
        Write-Host "    $(ConvertTo-Json $responseObj -Depth 10)"
        return $responseObj
    }

    return $null
}

Write-Host "Connecting to named pipe: \\.\pipe\$PipeName..." -ForegroundColor Yellow
$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

try {
    $pipe.Connect(5000)  # 5 second timeout
    Write-Host "Connected!" -ForegroundColor Green

    # Send Hello
    $writer = [System.IO.StreamWriter]::new($pipe)
    $writer.AutoFlush = $true
    $reader = [System.IO.StreamReader]::new($pipe)

    $hello = @{
        type = "Hello"
        request_id = "hello-1"
    }
    $writer.WriteLine((ConvertTo-Json $hello -Compress))
    $helloResponse = $reader.ReadLine()
    Write-Host "Hello response received ($(($helloResponse).Length) bytes)" -ForegroundColor Green

    Start-Sleep -Seconds 1

    # Test 1: Query current skirmish setup
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 1: Query skirmish setup state" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    Send-AdapterCommand -Pipe $pipe -Command "Game.Query" -Args @{path = "game.skirmish_setup"}

    Start-Sleep -Seconds 1

    # Test 2: List menu controls
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 2: List menu controls" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    Send-AdapterCommand -Pipe $pipe -Command "Menu.ListControls" -Args @{include_hidden = $false}

    Start-Sleep -Seconds 1

    # Test 3: Set individual slot
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 3: Set slot 1 to brutal AI" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    Send-AdapterCommand -Pipe $pipe -Command "Skirmish.SetSlot" -Args @{slot = 1; state = "brutal_ai"; color = 1; template = 1}

    Start-Sleep -Seconds 1

    # Test 3b: Set starting cash
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 3b: Set starting cash to 10000" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    Send-AdapterCommand -Pipe $pipe -Command "Skirmish.SetStartingCash" -Args @{cash = 10000}

    Start-Sleep -Seconds 1

    # Test 4: Query again to verify
    Write-Host "`n$('='*60)" -ForegroundColor Magenta
    Write-Host "TEST 4: Query skirmish setup again to verify" -ForegroundColor Magenta
    Write-Host "$('='*60)" -ForegroundColor Magenta
    Send-AdapterCommand -Pipe $pipe -Command "Game.Query" -Args @{path = "game.skirmish_setup"}

    Write-Host "`n$('='*60)" -ForegroundColor Green
    Write-Host "All tests complete!" -ForegroundColor Green
    Write-Host "$('='*60)" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
    Write-Host "`nConnection closed." -ForegroundColor Yellow
}
