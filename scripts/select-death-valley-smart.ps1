param(
    [string]$PipeName = "zh_ai_control"
)

$ErrorActionPreference = "Stop"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Smart Death Valley Selection" -ForegroundColor Cyan
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
    Start-Sleep -Seconds 1

    # Navigate to Skirmish Menu
    Write-Host "==> Navigating to Skirmish Menu..." -ForegroundColor Magenta
    $clickSP = '{"type":"SessionCommand","request_id":"nav-1","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSinglePlayer"}}'
    $writer.WriteLine($clickSP)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    $clickSkirmish = '{"type":"SessionCommand","request_id":"nav-2","cmd":"Menu.Click","args":{"controlId":"MainMenu.wnd:ButtonSkirmish"}}'
    $writer.WriteLine($clickSkirmish)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 3

    # Open Map Selection Menu
    Write-Host "==> Opening Map Selection Menu..." -ForegroundColor Magenta
    $clickSelectMap = '{"type":"SessionCommand","request_id":"nav-3","cmd":"Menu.Click","args":{"controlId":"SkirmishGameOptionsMenu.wnd:ButtonSelectMap"}}'
    $writer.WriteLine($clickSelectMap)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    # Query the listbox contents
    Write-Host "==> Querying available maps..." -ForegroundColor Magenta
    $getListBox = '{"type":"SessionCommand","request_id":"query-1","cmd":"Menu.GetListBoxContents","args":{"controlId":"SkirmishMapSelectMenu.wnd:ListboxMap"}}'
    $writer.WriteLine($getListBox)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    # QueryResult has type "QueryResult" and a result field
    # QueryError has type "QueryError" and error_type field
    if ($respObj.type -eq "QueryError") {
        Write-Host "    Failed to query listbox: $($respObj.error_type) - $($respObj.reason)" -ForegroundColor Red
        exit 1
    }

    if ($respObj.type -ne "QueryResult") {
        Write-Host "    Unexpected response type: $($respObj.type)" -ForegroundColor Red
        Write-Host "    Response: $resp" -ForegroundColor Gray
        exit 1
    }

    Write-Host "    Found $($respObj.result.count) maps" -ForegroundColor Green

    # Find Death Valley
    $deathValleyIndex = -1
    foreach ($item in $respObj.result.items) {
        Write-Host "    [$($item.index)] $($item.map)" -ForegroundColor Gray
        if ($item.map -like "*Death*Valley*") {
            $deathValleyIndex = $item.index
            Write-Host "      ^^ FOUND Death Valley at index $deathValleyIndex!" -ForegroundColor Green
        }
    }

    if ($deathValleyIndex -eq -1) {
        Write-Host "`nDeath Valley not found in map list!" -ForegroundColor Red
        exit 1
    }

    # Select Death Valley
    Write-Host "`n==> Selecting Death Valley (index $deathValleyIndex)..." -ForegroundColor Magenta
    $selectMap = "{`"type`":`"SessionCommand`",`"request_id`":`"select-1`",`"cmd`":`"Menu.SelectListBox`",`"args`":{`"controlId`":`"SkirmishMapSelectMenu.wnd:ListboxMap`",`"index`":$deathValleyIndex}}"
    $writer.WriteLine($selectMap)
    $resp = $reader.ReadLine()
    $respObj = ConvertFrom-Json $resp

    if ($respObj.ok) {
        Write-Host "    Death Valley selected!" -ForegroundColor Green
    } else {
        Write-Host "    Failed to select: $($respObj.reason)" -ForegroundColor Red
        exit 1
    }
    Start-Sleep -Seconds 1

    # Click OK
    Write-Host "==> Clicking OK to confirm..." -ForegroundColor Magenta
    $clickOK = '{"type":"SessionCommand","request_id":"nav-4","cmd":"Menu.Click","args":{"controlId":"SkirmishMapSelectMenu.wnd:ButtonOK"}}'
    $writer.WriteLine($clickOK)
    $resp = $reader.ReadLine()
    Start-Sleep -Seconds 2

    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "Death Valley selected successfully!" -ForegroundColor Green
    Write-Host "Check the game UI to verify." -ForegroundColor Green
    Write-Host "========================================`n" -ForegroundColor Green

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    if ($pipe -and $pipe.IsConnected) {
        $pipe.Close()
    }
    $pipe.Dispose()
}
