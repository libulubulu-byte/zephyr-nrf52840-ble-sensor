# =============================================================================
#  Wait for the NICENANO volume to appear, then flash and verify.
#
#  Use this when the running application is a Zephyr build, because Zephyr's
#  CDC ACM does not implement the Adafruit 1200 bps touch - the only way in is
#  a physical double-tap of the reset pad.
#
#      powershell -File scripts\flash_wait.ps1 [-TimeoutSec 300]
#      powershell -File scripts\flash_wait.ps1 -Image firmware\power_test.uf2
#
#  -Image selects which UF2 to write, so the bare-board current probe can be
#  flashed with the same procedure as the product build.
# =============================================================================
param(
    [int]$TimeoutSec = 300,
    [string]$Image = 'firmware\zephyr.uf2'
)

$ErrorActionPreference = 'Continue'
$root = 'D:\ESP32-IDF\ESP32S3_Zephyr'
$uf2  = Join-Path $root $Image
$log  = "$root\docs\console_boot.log"

function Get-Nicenano { Get-Volume | Where-Object { $_.FileSystemLabel -eq 'NICENANO' } }
function Find-Port([int]$targetPid) {
    $dev = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.DeviceID -match ("VID_239A&PID_{0:X4}" -f $targetPid) -and $_.Name -match '(COM\d+)' }
    if ($dev) { return [regex]::Match($dev.Name, 'COM\d+').Value }
    return $null
}

if (-not (Test-Path $uf2)) { Write-Output "FATAL: $uf2 not found - build first"; exit 1 }
$size = (Get-Item $uf2).Length
Write-Output "image: $uf2 ($size bytes)"

Write-Output "=== waiting up to $TimeoutSec s for the NICENANO volume ==="
Write-Output "    (double-tap the reset pad: short RST to GND twice, ~0.5 s apart)"
$found = $false
for ($i = 1; $i -le $TimeoutSec; $i++) {
    if (Get-Nicenano) { Write-Output "  volume appeared after $i s"; $found = $true; break }
    if ($i % 15 -eq 0) { Write-Output "  ... $i s" }
}
if (-not $found) { Write-Output "timed out - no volume"; exit 1 }

$drive = (Get-Nicenano).DriveLetter
Write-Output "=== copying to ${drive}:\zephyr.uf2 ==="
Copy-Item $uf2 "${drive}:\zephyr.uf2" -Force

for ($i = 1; $i -le 40; $i++) {
    Start-Sleep -Seconds 1
    if (-not (Get-Nicenano)) { Write-Output "  volume gone after $i s -> programmed"; break }
}

Write-Output "=== waiting for the application to enumerate ==="
$appPort = $null
for ($i = 1; $i -le 40; $i++) {
    Start-Sleep -Seconds 1
    $appPort = Find-Port 0x8029
    if ($appPort) { Write-Output "  application on $appPort after $i s"; break }
}
if (-not $appPort) {
    # Not necessarily a failure: the power_test probe deliberately has the USB
    # stack switched off, so it never enumerates a CDC port. Feedback there is
    # the LED (4 blinks at power-up) instead of the console.
    Write-Output "no CDC port appeared."
    Write-Output "If you flashed firmware\power_test.uf2 that is expected - the probe"
    Write-Output "has no USB on purpose. Unplug USB, feed the board through the meter,"
    Write-Output "and watch the LED: 4 blinks = running, then 30 s later 1 blink = the"
    Write-Output "transition to System OFF."
    Write-Output "DONE"
    exit 0
}

Write-Output "=== capturing the boot sequence (30 s) ==="
$lines = @()
try {
    $sp = New-Object System.IO.Ports.SerialPort $appPort, 115200
    $sp.ReadTimeout = 800
    $sp.Open()
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        try {
            $l = $sp.ReadLine()
            $clean = $l -replace '\x1b\[[0-9;]*[A-Za-z]', '' -replace '^\s+|\s+$', ''
            if ($clean) { $lines += $clean; Write-Output "  | $clean" }
        } catch {}
    }
    $sp.Close(); $sp.Dispose()
} catch {
    Write-Output "  console read failed: $($_.Exception.Message)"
}
if ($lines.Count -gt 0) { $lines | Set-Content -Path $log -Encoding UTF8; Write-Output "  saved to $log" }
Write-Output "DONE"
