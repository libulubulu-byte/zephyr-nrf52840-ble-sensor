# =============================================================================
#  Capture the application console for N seconds and save it (ANSI stripped).
#    powershell -File scripts\capture_console.ps1 [seconds]
# =============================================================================
param([int]$Seconds = 45)
$ErrorActionPreference = 'Continue'
$log = 'D:\ESP32-IDF\ESP32S3_Zephyr\docs\console_boot.log'

$dev = Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.DeviceID -match 'VID_239A&PID_8029' -and $_.Name -match '(COM\d+)' } | Select-Object -First 1
if (-not $dev) { Write-Output "application port (239A:8029) not present"; exit 1 }
$port = [regex]::Match($dev.Name, 'COM\d+').Value
Write-Output "capturing $port for $Seconds s"

$sp = New-Object System.IO.Ports.SerialPort $port, 115200
$sp.ReadTimeout = 500
$out = New-Object System.Collections.Generic.List[string]
try {
    $sp.Open()
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $l = $sp.ReadLine()
            # strip ANSI escape sequences so the log is readable in a browser
            $clean = $l -replace '\x1b\[[0-9;]*[A-Za-z]', '' -replace '^\s+|\s+$', ''
            if ($clean) { $out.Add($clean); Write-Output "| $clean" }
        } catch {}
    }
    $sp.Close(); $sp.Dispose()
} catch {
    Write-Output "open failed: $($_.Exception.Message)"
    exit 2
}

if ($out.Count -gt 0) {
    $out | Set-Content -Path $log -Encoding UTF8
    Write-Output "saved $($out.Count) lines to $log"
}
Write-Output "DONE"
