param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Testing Additional Skirmish Controls" -ForegroundColor Cyan

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

    # List all controls to find starting cash, start position, and game speed controls
    Write-Host "==> Listing all combo boxes and sliders..." -ForegroundColor Magenta
    $listControls = '{"type":"SessionCommand","request_id":"list-1","cmd":"Menu.ListControls","args":{"include_hidden":false}}'
    $writer.WriteLine($listControls)

    # Read response with timeout handling
    $resp = ""
    $startTime = Get-Date
    while ([string]::IsNullOrEmpty($resp) -and ((Get-Date) - $startTime).TotalSeconds -lt 10) {
        if ($reader.Peek() -ge 0) {
            $resp = $reader.ReadLine()
            break
        }
        Start-Sleep -Milliseconds 100
    }

    if ([string]::IsNullOrEmpty($resp)) {
        Write-Host "Response timed out!" -ForegroundColor Red
    } else {
        $respObj = ConvertFrom-Json $resp

        Write-Host "`nCombo boxes:" -ForegroundColor Yellow
        $respObj.result.controls | Where-Object { $_.type -eq "combo_box" -and $_.controlId -like "*Cash*" } | ForEach-Object {
            Write-Host "  $($_.controlId)" -ForegroundColor White
        }

        Write-Host "`nSliders:" -ForegroundColor Yellow
        $respObj.result.controls | Where-Object { $_.type -eq "slider" } | ForEach-Object {
            Write-Host "  $($_.controlId)" -ForegroundColor White
        }

        Write-Host "`nButtons (for start positions):" -ForegroundColor Yellow
        $respObj.result.controls | Where-Object { $_.type -eq "button" -and $_.controlId -like "*Position*" } | ForEach-Object {
            Write-Host "  $($_.controlId)" -ForegroundColor White
        }
    }

    Write-Host "`n==> Testing Starting Cash combo box..." -ForegroundColor Magenta
    # Try to select 50000 cash (need to find the right index)
    $cashControlIds = @(
        "SkirmishGameOptionsMenu.wnd:ComboBoxStartingCash",
        "ComboBoxStartingCash"
    )

    foreach ($cbId in $cashControlIds) {
        Write-Host "  Trying $cbId..." -ForegroundColor Yellow
        # Try different indices to find 50000
        for ($i = 0; $i -le 10; $i++) {
            $cmd = "{`"type`":`"SessionCommand`",`"request_id`":`"cash-$i`",`"cmd`":`"Menu.SelectComboBox`",`"args`":{`"controlId`":`"$cbId`",`"index`":$i}}"
            $writer.WriteLine($cmd)
            $resp = $reader.ReadLine()
            $respObj = ConvertFrom-Json $resp

            if ($respObj.ok) {
                Write-Host "    Index $i -SUCCESS" -ForegroundColor Green
            } else {
                Write-Host "    Index $i -$($respObj.reason)" -ForegroundColor Red
                break
            }
            Start-Sleep -Milliseconds 300
        }
        break
    }

    Write-Host "`nTest complete - check screenshot to see which cash amount is selected" -ForegroundColor Green

} catch {
    Write-Host "Error -$_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
