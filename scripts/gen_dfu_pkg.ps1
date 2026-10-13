# =============================================================================
#  Build a DFU package for the Adafruit bootloader from the Zephyr hex.
#
#  Parameters are copied from a package that is known to flash on this board
#  (the DRV2605L project's SyncTrigger_A.zip):
#      device_type     82    -> 0x52, nRF52840
#      softdevice_req  [182] -> 0xB6, S140 v7.3.0
#
#  Our image has no SoftDevice dependency of its own (Zephyr carries its own
#  link layer), so sd-req is set to 0x00 = "no SoftDevice required". The app
#  still lands at 0x26000 because that is where the bootloader writes, which
#  is exactly what the binary is linked for.
# =============================================================================
$ErrorActionPreference = 'Continue'

$nrfutil = 'C:\Users\DELL\AppData\Local\Arduino15\packages\adafruit\hardware\nrf52\1.7.0\tools\adafruit-nrfutil\win32\adafruit-nrfutil.exe'
$hex     = 'D:\ESP32-IDF\ESP32S3_Zephyr\firmware\zephyr.hex'
$zip     = 'D:\ESP32-IDF\ESP32S3_Zephyr\firmware\zephyr_dfu.zip'

if (-not (Test-Path $nrfutil)) { Write-Output "FATAL: nrfutil not found"; exit 1 }
if (-not (Test-Path $hex))     { Write-Output "FATAL: $hex not found"; exit 1 }

Remove-Item $zip -Force -ErrorAction SilentlyContinue

Write-Output "=== first 8 bytes of the hex (address of the first record) ==="
Get-Content $hex -TotalCount 3

Write-Output "=== genpkg (sd-req 0x00 = no SoftDevice required) ==="
& $nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x00 --application $hex $zip
Write-Output "genpkg exit=$LASTEXITCODE"

if (-not (Test-Path $zip)) { Write-Output "FATAL: package not produced"; exit 1 }

$tmp = Join-Path $env:TEMP 'dfu_zephyr'
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive -Path $zip -DestinationPath $tmp -Force
Write-Output "=== package contents ==="
Get-ChildItem $tmp | Select-Object Name, Length | Format-Table -AutoSize
Write-Output "=== manifest ==="
Get-Content (Join-Path $tmp 'manifest.json') -Raw
Write-Output "DONE"
