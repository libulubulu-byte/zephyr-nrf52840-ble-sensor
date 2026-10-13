# =============================================================================
#  Flash the Zephyr image onto the SuperMini, handling both bootloader modes.
#
#  Observed on this hardware: the 1200 bps touch reboots into the Adafruit
#  bootloader, which first presents as a CDC-only device (239A:00B3) and only
#  later exposes the NICENANO mass-storage volume. So this script waits for
#  either, and falls back to the serial DFU transport if MSC never shows up.
#
#      powershell -File scripts\flash.ps1 [-NoBuild]
#
#  Reversible: copy the DRV2605L project's firmware\out\SyncTrigger_A.uf2 back
#  onto the NICENANO volume to restore the previous firmware.
# =============================================================================
param([switch]$NoBuild)

$ErrorActionPreference = 'Continue'
$root    = 'D:\ESP32-IDF\ESP32S3_Zephyr'
$uf2     = "$root\firmware\zephyr.uf2"
$hex     = "$root\firmware\zephyr.hex"
$zip     = "$root\firmware\zephyr_dfu.zip"
$log     = "$root\docs\console_boot.log"
$nrfutil = 'C:\Users\DELL\AppData\Local\Arduino15\packages\adafruit\hardware\nrf52\1.7.0\tools\adafruit-nrfutil\win32\adafruit-nrfutil.exe'

function Get-Nicenano { Get-Volume | Where-Object { $_.FileSystemLabel -eq 'NICENANO' } }
function Find-Port([int]$targetPid) {
    $dev = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.DeviceID -match ("VID_239A&PID_{0:X4}" -f $targetPid) -and $_.Name -match '(COM\d+)' }
    if ($dev) { return [regex]::Match($dev.Name, 'COM\d+').Value }
    return $null
}

# ---------------------------------------------------------------- build
if (-not $NoBuild) {
    Write-Output "=== rebuilding in WSL ==="
    & wsl.exe -d Ubuntu-22.04 -- bash /mnt/d/ESP32-IDF/ESP32S3_Zephyr/scripts/build.sh 2>&1 |
        Select-Object -Last 8
}

# ---------------------------------------------------------------- enter bootloader
if (-not (Get-Nicenano)) {
    $app = Find-Port 0x8029
    if ($app) {
        Write-Output "=== 1200 bps touch on $app ==="
        try {
            $sp = New-Object System.IO.Ports.SerialPort $app, 1200
            $sp.DtrEnable = $true
            $sp.Open()
            Start-Sleep -Milliseconds 400
            $sp.Close(); $sp.Dispose()
            Write-Output "  touch sent"
        } catch {
            Write-Output "  touch failed: $($_.Exception.Message)"
        }
    } else {
        Write-Output "no application port; assuming the board is already in the bootloader"
    }

    Write-Output "=== waiting up to 120 s for NICENANO (or a DFU port) ==="
    for ($i = 1; $i -le 120; $i++) {
        Start-Sleep -Seconds 1
        if (Get-Nicenano) { Write-Output "  NICENANO appeared after $i s"; break }
        if ($i % 20 -eq 0) {
            $d = Find-Port 0x00B3
            if (-not $d) { $d = 'none' }
            Write-Output "  ... $i s  (dfu port: $d)"
        }
    }
}

# ---------------------------------------------------------------- write it
$vol = Get-Nicenano
if ($vol) {
    $drive = $vol.DriveLetter
    Write-Output "=== MSC mode: copying to ${drive}:\zephyr.uf2 ==="
    Copy-Item $uf2 "${drive}:\zephyr.uf2" -Force
    for ($i = 1; $i -le 40; $i++) {
        Start-Sleep -Seconds 1
        if (-not (Get-Nicenano)) { Write-Output "  volume gone after $i s -> programming done"; break }
    }
} else {
    $dfu = Find-Port 0x00B3
    if (-not $dfu) { Write-Output "FATAL: neither NICENANO nor a DFU port is present"; exit 1 }

    Write-Output "=== CDU/DFU mode: building the package and flashing over $dfu ==="
    Remove-Item $zip -Force -ErrorAction SilentlyContinue
    & $nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x00 --application $hex $zip | Out-Null
    if (-not (Test-Path $zip)) { Write-Output "FATAL: genpkg produced nothing"; exit 1 }

    # NOTE: adafruit-nrfutil prints a traceback and still exits 0, so the
    # output has to be inspected rather than trusting $LASTEXITCODE.
    $out = & $nrfutil dfu serial -pkg $zip -p $dfu -b 115200 --singlebank 2>&1
    $out | ForEach-Object { Write-Output "  $_" }
    if ($out -match 'Failed to upgrade|Traceback') {
        Write-Output "FATAL: DFU transport failed. The bootloader on this board prefers the UF2 volume."
        exit 1
    }
    Write-Output "  DFU reported success"
}

# ---------------------------------------------------------------- verify
Write-Output "=== waiting for the application to enumerate ==="
$appPort = $null
for ($i = 1; $i -le 40; $i++) {
    Start-Sleep -Seconds 1
    $appPort = Find-Port 0x8029
    if ($appPort) { Write-Output "  application on $appPort after $i s"; break }
}
if (-not $appPort) { Write-Output "application port never appeared"; exit 2 }

Write-Output "=== capturing the boot sequence (25 s) ==="
$lines = @()
try {
    $sp = New-Object System.IO.Ports.SerialPort $appPort, 115200
    $sp.ReadTimeout = 800
    $sp.Open()
    $deadline = (Get-Date).AddSeconds(25)
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
