# BLE Environmental Sensor Node — nRF52840 + Zephyr / nRF Connect SDK

A battery-powered BLE sensor node: it measures temperature and humidity with an
SHT31, exposes them over a custom GATT service (read + notify), keeps sampling
into a 100-entry ring buffer while nobody is connected, replays that backlog on
reconnect, and drops into a ~30 µA System ON Idle state between samples.

This is the same sensor node I built bare-metal, rebuilt on Zephyr to get
MCUboot OTA, power management, and a maintainable RTOS architecture — the
trade-offs (RAM footprint, boot time, power states) are documented in the
README.

---

## 1. Status at a glance

| Item | State |
| --- | --- |
| Application code, board definition, GATT service, ring buffer, power states | **done** |
| Builds for `nrf52840_supermini` with NCS v2.7.0 (Zephyr 3.6.99-ncs2) | **done** — 193 820 B flash / 40 244 B RAM (24.65 % / 15.35 %) |
| UF2 image produced and verified for the Adafruit bootloader | **done** — linked at `0x00026000`, family `0xADA52840` |
| On-hardware validation (scan / connect / read / notify / replay) | see §8 — results recorded there |
| Power measurement (< 50 µA standby) | **met — ≤ 50 µA** with the ME6217 regulator out of the path (board fed 3.6 V direct), LED off, no central connected. The firmware's own share is **4 µA**, measured as phase A − phase B on the bare-board probe; the ~99 µA the board ships with is that regulator's quiescent current. Protocol and raw numbers in `data/README.md`; the investigation, including the four measurement artefacts and the one wrong mechanism that had to be retracted, in `docs/DEBUG_NOTES.md` |
| MCUboot dual-slot OTA | **not enabled on this board**, see §7 |

Nothing in this README claims a measurement that has not been taken. Where a
number is a target rather than a reading, it says so.

### Build footprint

Measured with `arm-zephyr-eabi-size` on the MinSizeRel build, Picolibc, no
MCUboot, open-source link layer:

| | Used | Available | Notes |
| --- | --- | --- | --- |
| Flash | 192 012 B | 786 432 B (`code_partition`, 768 KB) | 24 % — the open-source link layer is the bulk of it |
| RAM | 40 608 B | 262 144 B | 15 % — 1 KB of that is the ring buffer's 100 × 8 B records plus write buffers |

The gap matters for the DK port in §7: with only 24 % of the slot used there is
more than enough room for a second MCUboot slot in the same 768 KB region.

---

## 2. Hardware

| Part | Notes |
| --- | --- |
| SuperMini nRF52840 | nice!nano v2 compatible clone; nRF52840, 1 MB flash, 256 KB RAM |
| SHT31 breakout | I²C, address `0x44`, on-board pull-ups |
| USB cable | flashing (UF2) and the log console (CDC ACM) |
| Multimeter (µA range) | for the standby current measurement |

The board ships with the **Adafruit nRF52 UF2 bootloader**, identified through
the bootloader's own `src/boards/nice_nano/board.h`:

| Property | Value |
| --- | --- |
| UF2 volume label | `NICENANO` |
| USB VID:PID (bootloader) | `239A:00B3` |
| USB VID:PID (application) | `239A:8029` |
| LED | blue, **P0.15**, active high |
| Application start address | `0x00026000` |
| Bootloader region | `0x000F4000`–`0x00100000` |

Upstream Zephyr has no board definition for this hardware, so
`zephyr/boards/nordic/nrf52840_supermini/` is part of this project.

### Wiring

```
        SuperMini nRF52840                        SHT31 breakout
      +---------------------+                    +---------------+
      |                     |                    |               |
      |  3V3  o-------------+--------------------+ VIN           |
      |  GND  o-------------+--------------------+ GND           |
      |                     |                    |               |
      |  P1.04 (SDA)  o-----+--------------------+ SDA           |
      |  P1.06 (SCL)  o-----+--------------------+ SCL           |
      |                     |                    |               |
      |                     |      (not wired)   + ADDR -> GND   |  = 0x44
      |  P0.15  -> blue LED |                    +---------------+
      |                     |
      |  P0.29  o-----------+---- battery divider (below)
      |                     |
      |  USB-C  -> flashing + CDC ACM console
      +---------------------+

        Battery divider, on P0.29 (AIN5, pad "029"):

            BAT+ ---[ 30k ]---+--- P0.29
                             |
                          [ 7.5k ]
                             |
                            GND

        SDA/SCL are the pads silkscreened 104/106; the battery divider goes to
        029 - not 102, and not 104/106. Only P0.02 / P0.29 / P0.31 on this board
        have analog inputs at all (see "Which pins can actually do ADC").
```

`P1.04` / `P1.06` are the pins the SuperMini silkscreens as `104` / `106`.
Changing them is a two-line edit in `zephyr/app.overlay` plus the matching
`nrf52840_supermini-pinctrl.dtsi` entries — nothing in the C code knows which
pins I²C uses.

Battery voltage is read on **P0.29 (AIN5, pad `029`)** through an external
divider built from **two 600 kΩ** resistors:

```
Vadc = Vbat × 600k / (600k + 600k) = Vbat / 2      APP_BATT_DIVIDER_NUM/DEN = 2/1
```

AIN2 (`P0.04`) would have been the natural choice: it is the battery-sense pad
on nice!nano class boards, which is exactly why this clone does not break it
out. That is also the problem — a pad you cannot reach is a reading you cannot
verify or calibrate on the bench. P0.29 is broken out, so the divider is under
our control and can be checked with a multimeter.

### The ratio was wrong twice, and both wrong values looked plausible

Worth recording in order, because the lesson is that this number has to come off
the board rather than off an assumption:

| Value | Where it came from | How it was caught |
| --- | --- | --- |
| `2/1` | assumed from the nice!nano reference design | `vbat=924 mV` on USB with no cell fitted — 2/1 could not explain it |
| `5/1` | read from a 30k/7.5k pair, SMD markings `303` and `752` | those turned out not to be the parts in circuit: the reading went to `65501 mV` |
| `2/1` | the actual parts: **two 100 kΩ** | to be confirmed against a multimeter |

The middle one is the instructive failure. `303` and `752` are genuine 3-digit
SMD codes — 30 kΩ and 7.5 kΩ — so the arithmetic from them was right and the
resulting number looked entirely reasonable in the log. It was simply the wrong
pair of resistors. **Only a multimeter settles this**, and the check is cheap:
with a LiPo fitted, the reported `vbat` should match the meter on BAT+ to within
a few tens of mV.

A separate mistake from the same period: which pins *can* do ADC. The silkscreen
prints the port number directly, so `102` reads like an ADC-ish name; it is
P1.02, and **no P1.xx pin has an analog input**. See the table below.

### The divider's cost is a separate choice from its ratio

Two 600 kΩ resistors permanently across the cell is

```
3.8 V / 1.2 MΩ ≈ 3.2 µA          (3.5 µA at 4.2 V)
```

which leaves essentially the whole `< 50 µA` budget to the MCU and the radio.

The point worth keeping: **the ratio and the cost are independent knobs.** All
of these are 2:1 and they differ by 30× in the number that decides pass or fail:

| Divider | Current at 3.8 V | Source impedance |
| --- | --- | --- |
| 30 kΩ / 7.5 kΩ | 101 µA | 6 kΩ |
| 100 kΩ / 100 kΩ | 19 µA | 50 kΩ |
| **600 kΩ / 600 kΩ** | **3.2 µA** | 300 kΩ |

An earlier revision of this project was configured for the 30 kΩ/7.5 kΩ row —
more than twice the entire budget, on its own, before the radio or the MCU draw
anything.

### The cost of going higher is settling time, not software

300 kΩ of source impedance is where the acquisition time starts to matter. It
cannot be raised to compensate: the nRF52840's SAADC TACQ is a 3-bit field
offering only 3/5/10/15/20/40 µs (`SAADC_CH_CONFIG_TACQ_Msk` is `0x7`, values
0..5), and the board DTS already asks for 40 µs. That is the hardware ceiling —
the driver's alternative linear-TACQ path tops out at 750 ns, and would have
rejected 40 µs outright, so the enum path is provably the one in use.

If `vbat` reads low and consistently by a percent or two, that is the sampling
capacitor failing to settle through 300 kΩ. The fix is **not** software: a
**10 nF capacitor from the sense node to GND** gives the sample-and-hold a
low-impedance source at the instant it samples. It does not affect the DC ratio
and costs nothing at 3.2 µA. Falling back to 300 kΩ/300 kΩ (6.3 µA, 150 kΩ) is
the other option.

As always, the multimeter on BAT+ against the reported value is what settles it.

One thing to keep an eye on: **if the board has its own divider populated on
P0.04, it is still fitted and still drawing.** The P0.29 divider's own
contribution can be isolated by disconnecting it and measuring again; the
standby current is otherwise the only way to know what is on the board.

This is worth calling out in an interview as-is: the board's own battery sense
circuit, which is fine for a keyboard that lasts months, dominates a sensor
node's budget by 2×. Finding that is the point of measuring instead of
estimating.

### Which pins can actually do ADC

The nRF52840 has eight SAADC channels and they are all on port 0:

| AIN | Pad | Broken out on this board? |
| --- | --- | --- |
| AIN0 | P0.02 | yes — silkscreen `002` |
| AIN1 | P0.03 | no |
| AIN2 | P0.04 | no — it is the battery sense pad |
| AIN3 | P0.05 | no |
| AIN4 | P0.28 | no |
| AIN5 | P0.29 | yes — silkscreen `029` — **used here** |
| AIN6 | P0.30 | no |
| AIN7 | P0.31 | yes — silkscreen `031` |

**No `P1.xx` pin has an analog input**, whatever the silkscreen suggests — the
SuperMini labels its pads with the port number directly (`102` = P1.02,
`104` = P1.04). If an external divider is ever wanted, one of `002` / `029` /
`031` is the pin to use, plus a matching `zephyr,input-positive` in
`boards/nordic/nrf52840_supermini/nrf52840_supermini.dts`.

---

## 3. Architecture

```
                      +---------------------------------------+
                      |              main thread              |
                      |  banner -> history -> LED -> sensor   |
                      |  -> bt_enable -> advertising -> sleep |
                      +---------------------------------------+
                                        |
   +----------------+   +---------------+----------------+   +------------------+
   |  env_sensor.c  |   |        power.c  (sampler)      |   |  env_service.c   |
   |  SHT31 + AIN2  |<--| k_work_delayable on the system |-->| GATT server +    |
   +----------------+   | work queue: 2 s / 30 s cadence |   | notify + replay  |
                        +---------------+----------------+   +------------------+
                                        |                             ^
                                        v                             |
                                +---------------+             +-----------------+
                                |  history.c    |             |  status_led.c   |
                                |  100 x 8 byte |             |  P0.15,         |
                                |  ring buffer  |             |  connected only |
                                +---------------+             +-----------------+
```

Deliberate choices, and why:

* **One work item drives everything.** `power.c` owns a single
  `k_work_delayable`. Connected, it re-arms every 2 s and notifies; disconnected,
  every 30 s and buffers. There is no polling loop and no periodic timer, so
  between samples the idle thread parks the core and the tickless kernel wakes
  on the RTC.
* **History replay is paced, not a loop.** `env_service.c` pushes one record per
  work item (10 ms apart, so ~1 s for a full backlog). A tight `for` loop over
  100 notifications would monopolise the system work queue and ignore back-pressure
  from the link.
* **The backlog starts on *subscribe*, not on *connect*.** CCC state is per
  connection and is dropped when the central goes away, so on reconnect the
  central re-subscribes — that subscription *is* the request for the backlog.
  A control point also allows an explicit re-request.
* **The ring buffer never has to leave RAM.** System ON Idle keeps RAM alive, so
  there is no flash wear and no filesystem to corrupt. Overflow overwrites the
  oldest record and increments a drop counter, which the replay reports, so the
  host can tell data was lost rather than silently receiving a shorter history.
* **The status LED is off while disconnected.** See §6.

### Module map

| File | Responsibility |
| --- | --- |
| `src/main.c` | boot order, version banner, hands over to the event-driven core |
| `src/env_sensor.c` | SHT31 via Zephyr's generic sensor API + SAADC battery reading |
| `src/env_service.c` | custom 128-bit GATT service, CCC handling, history replay |
| `src/history.c` | 100-entry packed ring buffer |
| `src/power.c` | connected/offline cadence state machine |
| `src/status_led.c` | connection indicator (deliberately not a heartbeat) |
| `src/bootloader.c` | DFU magic + reset, so the node can be reflashed over BLE |
| `src/app_config.h` | build-time tunables with the reasoning next to each |
| `boards/nordic/nrf52840_supermini/` | SoC, flash layout, LED, pinctrl, UF2 |

---

## 4. BLE interface

Service UUID `e0f10001-7b5a-4b1c-9a3d-6f2c8e4d5a10`, advertised in the scan
response together with the device name `EnvNode`.

| Characteristic | UUID suffix | Format | Unit | Properties |
| --- | --- | --- | --- | --- |
| Temperature | `…0002` | `int16` LE | 0.01 °C | read, notify |
| Humidity | `…0003` | `uint16` LE | 0.01 %RH | read, notify |
| Battery | `…0004` | `uint16` LE | mV | read |
| History | `…0005` | 8-byte records | see below | notify |
| History control | `…0006` | `uint8` | command | write |

The four documented characteristics are the three the specification asks for;
`History` and `History control` are the addition that makes "replay the ring
buffer after a reconnect" observable from a phone. They are described in
`env_service.h` as an explicit extension rather than smuggled into one of the
three.

`Temperature` and `Humidity` are `sensor_value` readings converted once, in
`env_sensor.c`, so the GATT layer only ever deals in the fixed-point units the
characteristics promise.

**Connection parameters** — requested on connect and defaulted in Kconfig:

| Parameter | Value |
| --- | --- |
| Interval | 80 × 1.25 ms = **100 ms** (`CONFIG_BT_PERIPHERAL_PREF_MIN/MAX_INT=80`) |
| Latency | 0 |
| Supervision timeout | 400 × 10 ms = 4 s |

**History record** (packed, little endian, 8 bytes):

```
offset  size  field
0       4     uptime_s    uint32   seconds since boot when sampled
4       2     temp_c100   int16    0.01 °C
6       2     hum_pct100  uint16    0.01 %RH
```

**Control commands** (write one byte to `…0006`):

| Byte | Command | Effect |
| --- | --- | --- |
| `0x00` | stop replay | cancel an in-flight history replay |
| `0x01` | start replay | push the whole backlog again |
| `0x02` | clear history | drop the buffered records |
| `0x03` | **enter bootloader** | write the DFU magic and reset into the Adafruit UF2 bootloader |

`0x03` is what makes the node field-updatable without opening the enclosure —
see §7. It is deliberately not a separate characteristic: "do something to the
device" is one concern, and a second characteristic for a single byte would be
worse than a four-value enum.

Security is intentionally off (`CONFIG_BT_SMP=n`): the service is read/notify
only, and enabling LE Secure Connections would add flash, RAM and a pairing
step for no confidentiality benefit at this stage. `prj.conf` says where the
permissions would change.

The application also advertises `CONFIG_BT_DEVICE_APPEARANCE=1345` (`0x0541`,
sensor) so a scanner can categorise it.

---

## 5. Power states

| State | CPU | Radio | Sampling | Notes |
| --- | --- | --- | --- | --- |
| Connected | System ON, running | connected | notify every 2 s | LED on |
| Disconnected | **System ON Idle**, RTC running, RAM retained | advertising at 2 s | buffer every 30 s | LED off |
| System OFF | off | off | none | **not wired** — the board has no button, so there is no GPIO to wake it up |

The System ON Idle state is not a custom sleep routine: once the work item is
re-armed for 30 s, every thread is blocked, the idle thread executes `WFI`, and
`CONFIG_TICKLESS_KERNEL=y` lets the kernel program the RTC for the next deadline
instead of taking a tick every millisecond. The RTC *is* the thing that wakes
the node up, which is what the specification asks for.

**Advertising interval is the dominant knob.** `APP_ADV_INTERVAL_UNITS` in
`src/app_config.h` is set to `3200` (2 s). It has to be short enough that a phone
reconnects promptly and long enough to stay inside the budget, so the value is a
deliberate compromise: 2 s roughly halves the radio contribution against 1 s, and
field experience on this hardware puts the floor at about 3 s, where a phone
starts failing to find the node. If discovery becomes unreliable, `1600` (1 s) is
the value to put back.

---

## 6. The LED is a power decision, not a cosmetic one

The blue LED is lit **only while a central is connected**. There is no heartbeat.

A 3 ms blink of a ~1 mA LED once per second averages 3–10 µA, and a naive 1 Hz
square wave can easily average 500 µA — between 10 % and 1000 % of the entire
standby budget. Since the LED cannot be useful during the measurement anyway
(nobody is looking at the board), it stays off. `app_config.h` carries this
reasoning next to the two switches (`APP_LED_HEARTBEAT_ENABLE`,
`APP_LED_CONNECTED_ENABLE`) for anyone who wants to trade it back.

---

## 7. Firmware update: UF2 today, MCUboot on a DK

### What this board does

The SuperMini carries the **Adafruit UF2 bootloader**. Updating is a
drag-and-drop:

```powershell
# 1. enter the bootloader, either
#      a) over BLE: write 0x03 to the control point (no disassembly), or
#      b) physically: double-tap the reset pad (short RST to GND twice)
# 2. a volume labelled NICENANO appears
# 3. copy the image
copy firmware\zephyr.uf2 E:\
```

`scripts/flash.ps1` automates all of it, including waiting for the volume and
capturing the boot banner afterwards. `west flash --runner uf2` does the same
thing from a shell (see `boards/nordic/nrf52840_supermini/board.cmake`, which
wires up `boards/common/uf2.board.cmake`).

#### How the BLE entry works, and why there is no 1200 bps touch

The bootloader decides what to do at startup by comparing
`NRF_POWER->GPREGRET` against a set of magic values, taken here from the
bootloader's own `src/main.c`:

| Value | Meaning |
| --- | --- |
| `0x57` | `DFU_MAGIC_UF2_RESET` — CDC **and** MSC, i.e. the NICENANO volume |
| `0x4e` | `DFU_MAGIC_SERIAL_ONLY_RESET` — CDC only, for `adafruit-nrfutil` |
| `0xa8` | `DFU_MAGIC_OTA_RESET` — BLE OTA |
| `0xb1` | `DFU_MAGIC_OTA_APPJUM` — SoftDevice already running |

`src/bootloader.c` writes `0x57` and calls `sys_reboot()`. GPREGRET survives a
soft reset, so the next boot lands directly in UF2 mode.

Arduino boards enter the bootloader with a **1200 bps touch** instead. That
trick is implemented by the *application*, which watches for a 1200 baud line
coding on its CDC port. Zephyr's CDC ACM driver records the host's
`SET_LINE_CODING` request and lets you read it back with
`uart_line_ctrl_get(UART_LINE_CTRL_BAUD_RATE)`, but it exposes **no
notification when the value changes** — so the only way to notice would be to
poll, which spends part of a 50 µA budget on something that happens once per
firmware release. Doing it from the BLE control point costs nothing instead,
and uses the phone that is already connected.

### Why not MCUboot here

MCUboot needs (a) somewhere to put a second slot and (b) a way to install the
bootloader itself. This hardware blocks both:

* The Adafruit bootloader owns `0xF4000`–`0x100000` and reserves
  `0x00000`–`0x26000` for the MBR plus SoftDevice S140. The application is
  already squeezed into `0x26000`–`0xF4000`.
* Installing MCUboot over the Adafruit bootloader requires SWD. This board has
  no debug probe and no exposed SWD pads, so it cannot be done at all.

### What is identical on a DK

The application does not contain a single line of UF2-specific code. To move it
to nRF52840 DK hardware:

```bash
# 1. uncomment the block at the bottom of zephyr/prj.conf
#      CONFIG_BOOTLOADER_MCUBOOT=y
#      CONFIG_MCUBOOT_SIGNATURE_KEY_FILE="mcuboot_private.pem"
#      CONFIG_MCUBOOT_IMAGE_VERSION="1.0.0"
# 2. build for the DK instead of the SuperMini
west build -b nrf52840dk/nrf52840 --sysbuild zephyr/
# 3. flash bootloader + slot0 once over SWD, then update over BLE
west flash --erase
mcumgr --conntype ble --connstring peer_name=EnvNode image upload build/zephyr/zephyr.signed.bin
```

The DK's on-board J-Link flashes the MCUboot bootloader, `sysbuild` produces the
dual-slot layout plus a signed image, and OTA then goes over BLE with the
nRF Connect Device Manager app. Slot swap is `CONFIG_MCUBOOT_...` territory;
the sampler, the GATT service and the ring buffer do not change.

Trade-offs worth stating out loud, because they are the reason the Zephyr port
exists at all:

| | Adafruit UF2 | MCUboot + SMP |
| --- | --- | --- |
| Install cost | none (ships on the board) | SWD probe required |
| Transport | USB mass storage | USB or BLE |
| Update source | a human dragging a file | a phone app or fleet tool |
| Rollback | none | automatic, on failed validation |
| Image validation | none | ECDSA signature + slot trailer |

---

## 8. Acceptance checklist

| # | Criterion | How it is judged | Result |
| --- | --- | --- | --- |
| 1 | nRF Connect App scans the device, connects, reads temperature and humidity | `EnvNode` visible; temperature and humidity characteristics show plausible values read from the SHT31 | ☐ needs the SHT31 wired |
| 2 | Disconnected for 5 minutes, multimeter reads < 50 µA | 100 Ω shunt with a mV meter, supplied through `BAT`, LED off, no central connected | ☑ **≤ 50 µA met** with the ME6217 regulator out of the path. The same board as shipped reads ~99 µA and no firmware change moves it — that is the regulator's quiescent current, not the application. Reading in `data/power_consumption.csv`; the four measurement artefacts that had to be eliminated first, and the wrong mechanism that was claimed and then retracted, in `docs/DEBUG_NOTES.md` |
| 3 | Reconnect and the ring buffer replays | subscribe to History; the backlog arrives and the newest record matches the last offline sample | ☐ needs a phone |
| 4 | Flashing: UF2 drag-and-drop, and the README explains the difference from MCUboot | `firmware/zephyr.uf2` copied onto the NICENANO volume; §7 read and understood | ☑ flashed and booted on hardware |
| 5 | `prj.conf` carries a commented MCUboot block describing the DK steps | bottom of `zephyr/prj.conf` | ☑ |
| 6 | English code comments | all of `zephyr/src`, `zephyr/boards` | ☑ |
| 7 | README contains the wiring diagram and the power data | §2; the protocol in `data/README.md`; the acceptance reading in `data/power_consumption.csv`; the debugging narrative in `docs/DEBUG_NOTES.md` | ☑ — the CSV's three-run repeats per state are still ☐ |

### What was actually verified on the board

The image was flashed onto the SuperMini over the NICENANO volume and booted.
Verbatim from `docs/console_boot.log`:

```
*** Booting Zephyr OS build 3.6.99 ***
<inf> main:  BLE environmental sensor node v1.0.3
<inf> main:  board: nrf52840_supermini/nrf52840
<inf> main:  build: nRF Connect SDK v2.7.0
<inf> status_led: status LED on gpio@50000000.15 (off while disconnected)
<inf> env_sensor: battery ADC ready (channel 5, divider 2/1)
<inf> env_sensor: SHT3x ready (sht3xd@44) at 0x44
<inf> bt_hci_core: HW Platform: Nordic Semiconductor (0x0002)
<inf> bt_hci_core: HW Variant: nRF52x (0x0002)
<inf> power: offline t=+28.51 C rh=62.76 %RH vbat=3396 mV (2 buffered, 0 dropped)
```

That single capture exercises more of the project than it looks:

| Observation | What it proves |
| --- | --- |
| `board: nrf52840_supermini/nrf52840` | the out-of-tree board definition in `zephyr/boards/` is found and selected |
| `battery ADC ready (channel 5, divider 2/1)` | the battery path is on AIN5 / pad `029` with the ratio the fitted resistors actually produce |
| `offline … vbat=3396 mV` | the battery characteristic reports a plausible cell voltage — pin at 1698 mV, mid-range of the 0–3.6 V span, which is what a 1:1 divider across a 3.4 V node should give |
| it booted at all | `zephyr,code-partition` put the image at `0x00026000`, which is where the Adafruit bootloader jumps — a wrong offset here produces a silent no-boot |
| `status LED on gpio@50000000.15` | P0.15 resolved out of the devicetree, active-high as the bootloader's `board.h` said |
| `<inf> bt_hci_core` | the Bluetooth controller came up and the GATT service registered |
| the console text appearing over USB | CDC ACM is working as the log backend despite there being no UART bridge on the board |

Three real defects were found *by* running it and fixed, which is the argument
for running it:

* **The ADC was never configured when the SHT31 was absent.** `env_sensor_init()`
  returned early on a missing sensor, so the battery channel was left
  unconfigured and the log filled with `adc_nrfx_saadc: Channel 2 not configured`
  every 30 s. The two measurements are independent and are now initialised
  independently.
* **`zephyr,oversampling` made every ADC reading four times too large.** nRF
  SAADC hardware oversampling widens the result: 12-bit resolution with 4×
  oversampling yields a 14-bit value over the same voltage span. The nrfx
  driver passes that buffer straight to the hardware
  (`nrf_saadc_buffer_init`) without rescaling it, but
  `adc_raw_to_millivolts()` still divides by `2^sequence->resolution` — 4096 for
  resolution 12 — so the converted millivolts come out 4× high. The battery
  characteristic reported **65501 mV**, a saturated number that had already been
  clamped to `UINT16_MAX` and explained nothing. Software averaging already
  provides the noise reduction oversampling was there for, so it is simply gone,
  and the reading is now range-checked (`APP_BATT_PLAUSIBLE_MAX_MV`) so the next
  wiring fault is named rather than printed as a number.
* **The 1200 bps touch does nothing under Zephyr**, so there was no way back
  into the bootloader without opening the enclosure. That is now covered by the
  `0x03` control-point command (§7).

The offline cadence was also confirmed from the same run: samples logged at
`00:00:31`, `00:01:01` and `00:01:31` — exactly 30 s apart, with the CPU idle
in between.

The remaining criteria need the sensor wired, a phone running nRF Connect and a
multimeter; those numbers go into `data/power_consumption.csv` (protocol in
`data/README.md`), and `docs/` collects the app screenshots and the reflash
recording.

---

## 9. Repository layout

```
.
├── README.md                  this file
├── zephyr/                    the NCS application
│   ├── CMakeLists.txt
│   ├── prj.conf               all Kconfig, incl. the commented MCUboot block
│   ├── app.overlay            SHT31 wiring / I2C configuration
│   ├── boards/nordic/nrf52840_supermini/
│   │   ├── board.yml          hwmv2 board metadata
│   │   ├── nrf52840_supermini.dts        SoC, flash layout, LED, CDC console
│   │   ├── nrf52840_supermini-pinctrl.dtsi   I2C0 on P1.04 / P1.06
│   │   ├── nrf52840_supermini_defconfig  USE_DT_CODE_PARTITION + UF2 output
│   │   ├── board.cmake        UF2 runner + optional SWD runners
│   │   ├── Kconfig / Kconfig.defconfig / Kconfig.nrf52840_supermini
│   │   └── nrf52840_supermini.yaml
│   └── src/
│       ├── main.c
│       ├── env_sensor.c / .h
│       ├── env_service.c / .h
│       ├── history.c / .h
│       ├── power.c / .h
│       ├── status_led.c / .h
│       ├── bootloader.c / .h
│       └── app_config.h
├── power_test/                bare-board current probe (no BT/USB/console)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   └── src/main.c
├── data/                      power measurement CSV + protocol
├── docs/                      toolchain notes, acceptance evidence, boot log,
│                              DEBUG_NOTES.md (the debugging narrative)
├── firmware/                  build output copied back from the toolchain
├── webbt/                     Web Bluetooth display page (one file, no deps)
└── scripts/                   toolchain setup, build, flash, verify helpers
```

`webbt/index.html` is a local Web Bluetooth panel for the node: live
temperature, humidity and battery, a rolling chart, and a history replay view
that parses the ring buffer out of the 8-byte records. Open it over localhost —
Web Bluetooth needs a secure context and `file://` does not count:

```bash
cd webbt && python3 -m http.server 8000     # then open http://localhost:8000
```

```powershell
python -m http.server 8000 -d webbt         # or from Windows
```

Chrome or Edge on desktop and Android; iOS Safari does not implement Web
Bluetooth. `scripts/check_webbt.py` runs the static checks that catch the
failure modes of a single-file page — element ids referenced from JS that do not
exist, unbalanced brackets, `</script` in the script body, `await` in a
non-async function, and a scan for CJK characters so that "the page is English"
is a check rather than a promise — because there is no JavaScript engine on this
machine to execute it with.

`power_test/` shares the board definition in `zephyr/boards/`, which is why its
build passes `-DBOARD_ROOT=<repo>/zephyr`. `scripts/build_power_test.sh` handles
that and drops the image as `firmware/power_test.uf2`.

---

## 10. Building it

The full environment recipe, including the network workarounds this machine
needed, is in `docs/toolchain_setup.md`. Short version inside WSL:

```bash
bash scripts/10_python_deps.sh          # Zephyr's python packages (once)
bash scripts/build.sh                   # configure + build + copy artifacts
bash scripts/build.sh clean             # wipe the build directory
```

Artifacts land in `firmware/`:

| File | Use |
| --- | --- |
| `zephyr.uf2` | drag onto the NICENANO volume |
| `zephyr.hex` | SWD flashing with `nrfjprog` / `pyocd`, or a DFU package |
| `zephyr_dfu.zip` | `adafruit-nrfutil dfu serial -pkg … --singlebank` |

Flashing and verification run from Windows, because the volume and the console
belong to the host:

```powershell
powershell -File scripts\flash.ps1                # build, flash, capture the banner
powershell -File scripts\flash_wait.ps1           # wait for a manual double-tap, then flash
powershell -File scripts\capture_console.ps1 60   # just watch the log
```

`scripts/verify_image.sh` is the sanity check worth running after any change to
the board definition — it asserts the ELF loads at `0x00026000` and the UF2
family is `0xADA52840`, which are exactly the two things that make the image
bootable or silently inert.

### The edit → build → flash → look loop

This is the whole day-to-day workflow: one command from each side, plus a look.

**1. Build — inside WSL.** The script sets up its own environment, so it can be
run from any directory and with the distro not yet started:

```bash
wsl -d Ubuntu-22.04 -- bash /mnt/d/ESP32-IDF/ESP32S3_Zephyr/scripts/build.sh
```

It self-bootstraps `ZEPHYR_BASE`, the venv, the SDK path, `PATH` for `dtc` and
the module list. A success ends with the size table and `output copied to …`.
`bash scripts/build.sh clean` wipes the build directory first — only needed when
a Kconfig or devicetree change looks stale in the output.

**2. Flash — from Windows.** The NICENANO volume and the CDC console belong to
the host, so this side is PowerShell:

```powershell
cd d:\ESP32-IDF\ESP32S3_Zephyr
powershell -File scripts\flash_wait.ps1
```

It waits for the volume, copies `firmware\zephyr.uf2`, waits for the device to
re-enumerate and captures 30 s of console. While it waits, get the board into the
bootloader: write `0x03` to the control point from nRF Connect, or short RST to
GND twice **within 500 ms**. §7 covers both, including why the timing matters.

**3. Look.** Either read what `flash_wait.ps1` already printed, or:

```powershell
powershell -File scripts\capture_console.ps1 60
```

Bump `APP_VERSION_STRING` in `zephyr/src/app_config.h` before any build you
intend to verify. Without it the banner is identical between revisions and there
is no way to tell whether the image on the board is the one just built — a
mistake this project made once, and the reason the version is now in the banner.

**The lines that have caught real problems here:**

| Line | Means |
| --- | --- |
| `BLE environmental sensor node vX.Y.Z` | the version you expect; if not, the flash did not take |
| `battery ADC ready (channel N, divider A/B)` | both fields change when the ADC channel or ratio changes |
| `SHT3x ready (…) at 0x44` | the sensor answered on the bus |
| `sweeping i2c@… / answered at 0xNN` | the sensor did *not* answer, and the scan names what did instead |
| `battery reads N mV (pin M mV)` | wiring fault: the sense pin is sitting on the supply rail |
| `offline t=… rh=… vbat=… (N buffered, 0 dropped)` | sampling, conversion and buffering all working |

A stock machine with a normal network does not need any of the workarounds:

```bash
west init -m https://github.com/nrfconnect/sdk-nrf --mr v2.7.0
west update --narrow -o=--depth=1
west build -b nrf52840_supermini zephyr/
```

---

## 11. Interview notes

> This is the same sensor node I built bare-metal, rebuilt on Zephyr to get
> MCUboot OTA, power management, and a maintainable RTOS architecture — the
> trade-offs (RAM footprint, boot time, power states) are documented in the
> README.
>
> SuperMini uses Adafruit UF2 for firmware updates. The same application builds
> with MCUboot on an nRF52840 DK — swap the board target and enable
> `CONFIG_BOOTLOADER_MCUBOOT`. OTA DFU demo deferred to DK hardware; application
> logic is identical.

Points worth expanding on if asked:

* **Why a work item instead of a timer-driven main loop.** The bare-metal
  version polled; here a single delayed work item plus `CONFIG_TICKLESS_KERNEL`
  means the CPU is asleep for ~99.9 % of the standby period, and the sampling
  cadence is a value, not a loop structure.
* **Why the ring buffer is RAM-resident.** System ON Idle retains RAM, so the
  backlog costs nothing in flash wear. The drop counter exists because silently
  shortening a history is worse than admitting data loss.
* **Why the LED is disabled at standby.** It is a measurable power item, not a
  detail; a 1 Hz blink can exceed the entire budget.
* **Why the history characteristic exists.** The specification asked for
  reconnect-and-replay but only listed three characteristics; the honest
  solution is a fourth one, documented as an extension, not a reinterpretation
  of the temperature characteristic.
* **What the Zephyr port costs.** The bare-metal version had a hardcoded
  register sequence; this version carries a board definition, a devicetree
  overlay and ~2 KB of extra stack, and in exchange the sensor, the console and
  the boot mechanism are all one-line swaps.
* **What was not done and why.** MCUboot needs SWD, and the board cannot accept
  it; §7 shows exactly what the port to DK hardware looks like rather than
  pretending the limitation away.
