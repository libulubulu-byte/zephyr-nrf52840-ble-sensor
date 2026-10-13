# Evidence to collect

This directory holds the artefacts that back up §8 of the main README. The
filenames below are what the README links to; drop the files in with exactly
those names and the checklist becomes verifiable rather than asserted.

## 0. `DEBUG_NOTES.md` — the debugging record

Not an artefact to collect but the write-up of two problems that only a running
board exposes: the four firmware defects found by running it (§1) and the
standby-current investigation, hypothesis by hypothesis (§2), including the four
readings that were artefacts of the rig and the one mechanism that was claimed
and then retracted by its own control.

Read it if you want to know how the numbers in `../data/` were arrived at — and
which of them were wrong on the way.

## 1. nRF Connect App — connection and live readings

**Files:** `nrfconnect_connected.png`, `nrfconnect_notify.png`

1. Scan; `EnvNode` should appear with the service UUID in the scan response.
2. Connect. Confirm the connection interval the app reports is **100 ms**
   (it is requested on connect, so this is a real assertion about the firmware,
   not a hope).
3. Open the service. Screenshot the three readable characteristics.
4. Tap the subscribe control on **Temperature** and **Humidity**; screenshot
   again with the values changing every 2 s.

Record the values and the indoor reference (a second thermometer, or a weather
app) so the numbers can be sanity-checked. A reading of `+21.43 °C / 45.87 %RH`
with the app showing raw integers `2143` / `4587` is the evidence that the
0.01 resolution conversion is right.

## 2. Ring buffer replay

**Files:** `nrfconnect_history_backlog.png`

1. Disconnect. Leave the node alone for **at least 5 minutes** (10 offline
   samples at the 30 s cadence).
2. Reconnect and subscribe to **History**.
3. The backlog arrives automatically on subscribe. Screenshot the 8-byte records.
4. Cross-check: the last record's `uptime_s` should be within ~30 s of the
   disconnect, and the values should track the room, not the values streamed in
   step 1 — that proves these came from the buffer and are not live reads.

The boot banner printed on the CDC ACM console also reports
`history replay finished (N records, 0 dropped)`; a log capture of that line is
worth keeping alongside the screenshot.

## 3. DFU / flashing

**Files:** `dfu_nicenano_volume.png`, `dfu_recording.mp4`

1. Double-tap the reset pad. Screenshot Windows Explorer showing the **NICENANO**
   volume.
2. Screen-record: drag `firmware/zephyr.uf2` onto the volume, the drive
   disappearing, the board rebooting, and the three-blink boot pattern.
3. To make the update visibly take effect, bump `APP_VERSION_STRING` in
   `src/app_config.h`, rebuild, and show the new version in the boot banner on
   the console.

That recording is the honest substitute for the MCUboot rollback demo: it shows
what this hardware can do. The MCUboot path is described in §7 of the README
rather than faked.

## 4. Power measurement

**Files:** a meter photo plus the filled-in CSV

See `../data/README.md` for the protocol. Only the CSV in `data/` is referenced
by the README; put a photo of the meter in series here if you want the reading
to be independently believable.

## 5. Console logs

Two captures already exist, both taken over the USB CDC ACM console with ANSI
escape sequences stripped:

| File | Window | Shows |
| --- | --- | --- |
| `console_boot.log` | boot + first sample, v1.0.3 | boot banner, board target, LED resolution, **the battery path working** (`channel 5, divider 2/1` and `vbat=3396 mV`), the SHT3x probe succeeding, Bluetooth controller init |
| `console_offline_cadence.log` | ~30–90 s after boot (older build) | the offline sampling cadence firing at exactly 30 s intervals, and the `adc_nrfx_saadc: Channel 2 not configured` line that exposed the ADC init bug |

Once the SHT31 is wired and a phone is connected, a third capture is worth
adding: the boot banner followed by

```
SHT31 ready (sht3xd@44)
battery ADC ready (channel 2, divider 2/1)
advertising as "EnvNode" (interval 3200 x 0.625 ms)
notify t=+21.43 C rh=45.87 %RH vbat=3987 mV (0 buffered, 0 dropped)
```

and, after disconnecting and letting it buffer,

```
offline t=+21.44 C rh=45.90 %RH vbat=3986 mV (1 buffered, 0 dropped)
...
replaying 10 buffered record(s)
history replay finished (10 records, 0 dropped)
```

### Capturing

The console is CDC ACM, so it can be read from either side:

```powershell
powershell -File scripts\capture_console.ps1 60     # Windows, saves to console_boot.log
```

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 115200   # inside WSL, needs usbipd
```

The Windows path is simpler — the port belongs to the host, and attaching it to
WSL with `usbipd` takes it away from anything on Windows. Note that Zephyr's log
backend only writes while something is reading, so a capture started after boot
misses the banner: to get the banner you have to be listening across a reset.
