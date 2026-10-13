$ErrorActionPreference = 'SilentlyContinue'

Write-Output "=== 239A devices present ==="
Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -match 'VID_239A' } |
    Select-Object Status, FriendlyName, InstanceId | Format-List
if (-not (Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_239A' })) {
    Write-Output "  (none - the board is either unplugged or crashed)"
}

Write-Output "=== volumes ==="
Get-Volume | Where-Object { $_.DriveLetter } |
    Select-Object DriveLetter, FileSystemLabel | Format-Table -AutoSize

Write-Output "=== processes that might hold a COM port ==="
Get-Process | Where-Object { $_.ProcessName -match 'arduino|serial|discovery|monitor|java|python|putty' } |
    Select-Object Id, ProcessName | Format-Table -AutoSize

Write-Output "=== can we open the port? ==="
$dev = Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.DeviceID -match 'VID_239A' -and $_.Name -match '(COM\d+)' } | Select-Object -First 1
if ($dev) {
    $p = [regex]::Match($dev.Name, 'COM\d+').Value
    Write-Output "  port: $p"
    try {
        $sp = New-Object System.IO.Ports.SerialPort $p, 115200
        $sp.ReadTimeout = 500
        $sp.Open()
        Write-Output "  opened OK, closing"
        $sp.Close(); $sp.Dispose()
    } catch {
        Write-Output "  open failed: $($_.Exception.Message)"
    }
} else {
    Write-Output "  no COM port for a 239A device"
}
Write-Output "DONE"
