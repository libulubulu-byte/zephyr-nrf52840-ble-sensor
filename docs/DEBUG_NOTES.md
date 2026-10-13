# Debug notes

Two kinds of problem had to be solved to get this node working: firmware defects
that only a running board exposes, and a standby-current investigation that spent
most of its time proving which numbers were *not* measurements of the node.

Both are collected here, in the order they were met, because the second half is
the part that is easiest to get wrong and hardest to notice.

**How this file relates to the others.** [`../data/README.md`](../data/README.md)
is the *procedure and the result sheet*: how to take the measurement, the three
rules that keep it honest, and the raw readings. This file is the *narrative* —
which hypothesis was held, what killed it, and what that cost. Where the two
disagree about a number, `data/README.md` is the one that came off a meter.

---

## 1. Firmware defects found by running it

### 1.1 The battery channel was never configured when the sensor was missing

`env_sensor_init()` returned early when the SHT31 did not answer, so the SAADC
battery channel was left unconfigured. The sensor is optional to bring-up; the
battery is not. The symptom was a log filling with

```
adc_nrfx_saadc: Channel 2 not configured
```

every 30 s, while the battery characteristic quietly never produced a value.

The two measurements are independent and are now initialised independently. The
general shape of the bug is worth naming: **one early `return` in an init path
that handled two unrelated resources.**

### 1.2 `zephyr,oversampling` made every ADC reading four times too large

Hardware oversampling on the nRF SAADC widens the result: 12-bit resolution with
4× oversampling yields a 14-bit value over the same voltage span. The nrfx driver
hands that buffer straight to the hardware (`nrf_saadc_buffer_init`) without
rescaling it, but `adc_raw_to_millivolts()` still divides by
`2^sequence->resolution` = 4096 — so every converted millivolts value came out
**4× high**.

The battery characteristic reported **65501 mV**: a number already clamped to
`UINT16_MAX` at the GATT boundary, which explains nothing to whoever is reading
the app.

Software averaging already provided the noise reduction oversampling was there
for, so the devicetree property is simply gone, and the reading is now
range-checked against `APP_BATT_PLAUSIBLE_MAX_MV` so that the next wiring fault is
*named* rather than printed as a saturated number.

### 1.3 The 1200 bps touch does nothing under Zephyr

Arduino cores put the board into its bootloader when the host opens the CDC port
at 1200 baud. That trick is implemented by the **application**, not the
bootloader.

Zephyr's CDC ACM driver records the host's `SET_LINE_CODING` request and lets you
read it back with `uart_line_ctrl_get(UART_LINE_CTRL_BAUD_RATE)` — but it exposes
**no notification when the value changes**. The only way to notice would be to
poll, and polling spends part of a 50 µA budget on an event that happens once per
firmware release.

The consequence was real: with the enclosure closed there was **no route back
into the bootloader** other than a double-tap on the reset pad. It is covered now
by the `0x03` control-point command (§4 of the main README), which writes the DFU
magic and resets — free, and it uses the phone that is already connected.

---

## 2. The standby-current investigation

### 2.1 The method: one board, measured twice

A probe application (`power_test/`) strips the firmware to nothing — no
Bluetooth, no USB, no console, no sensors — and walks through two states marked
by **elapsed time only**, because on this board there is no console and no LED
either:

| When | State |
| --- | --- |
| t = 0..120 s | **phase A** — System ON Idle, RAM retained, RTC running |
| t = 120 s onward | **phase B** — System OFF, CPU halted, every clock stopped, RST untouched |

Phase A against phase B is the whole trick: the same board, measured twice, with
only the chip's power state differing. Whatever current survives phase B cannot
be the firmware.

Result on this board:

| Phase | Chip state | Reading |
| --- | --- | --- |
| **A** (t = 60 s) | System ON Idle, RTC running, RAM retained | **103 µA** |
| **B** (t = 180 s) | System OFF — CPU halted, clocks stopped | **99 µA** |

$$103\ \mu A - 99\ \mu A = \mathbf{4\ \mu A}\ \text{for the MCU}$$

The nRF52840 datasheet puts System ON Idle with the RTC at about 2.35 µA, so 4 µA
is the right order of magnitude: **the sleeper works** — tickless kernel, LFXO,
parked pins and all. The firmware's share of the budget has a factor of twelve to
spare. Whether the product passes depends entirely on what the other ~99 µA is.

### 2.2 Four numbers that were not measurements of the node

Each of these was, at some point, written down as a property of the board before
being recognised as a property of the rig.

| Reading | What it actually was |
| --- | --- |
| **500 µA**, "firmware idling with RST held low" | the reset pin's internal ~10 kΩ pull-up, shorted to GND *through the meter*: 102 µA + ~400 µA |
| **400 µA at 3.3 V but 150 µA at 3.6 V** — current *falling* as voltage rises | the meter's µA-range burden resistance (1–10 kΩ in series), plus feeding a 3.3 V regulator from a 3.3 V supply, below the headroom the part needs |
| **hundreds of µA**, with no central connected | VBUS present: the LiPo charger live, and the USB peripheral initialised at boot with its ~1 kHz SOF interrupt |
| **"100 to 300 µA, jumping"**, reported as one number | a load that is not resistive, read with an instrument that cannot average it |

The fix for all four is a procedure rather than a part: a **100 Ω shunt** with the
meter on its mV range, supplied through `BAT` with the charger's input floating,
and **MIN/MAX/AVG** instead of a single number. It is written up as three rules in
[`../data/README.md`](../data/README.md).

**The lesson that generalises:** the µA range of a handheld DMM is the default
choice and it is the wrong one — it changes the circuit it measures.

### 2.3 The attribution that was wrong: the LED pin

A product build that left the LED pin disconnected measured **200 µA** where a
build that drove it measured **103 µA**. Two builds, one difference, 97 µA — that
looked like a mechanism, and it was written down as one: *the floating LED pin is
drawing the current.*

Then the control was asked. The bare-board probe parks every pin, so its LED pin
was already floating, and it read **99 µA either way**:

| P0.15 in the probe | Reading |
| --- | --- |
| disconnected (floating) | 99 µA |
| driven low | 99 µA |

No effect. The 97 µA did not reproduce, so the pin was never the mechanism — and
**one product reading was not enough to name one.** Two builds differ in one
respect; the number of things actually responsible can still be zero.

The value here is not the correction, it is the *method*: a mechanism claim needs
a control, and a control costs one flash.

### 2.4 P0.13 / HICHG: the one pin where measurement did produce a mechanism

| P0.13 | Reading |
| --- | --- |
| floating | 102 µA |
| driven low | 600 µA |

A configuration strap that swings the current by 500 µA belongs to a part that is
*running*. `HICHG` only means anything to a charger with an input, so the charger
had to be active — which points at the power path, and at the supply arrangement
used to take the reading, rather than at the chip.

Driving it low was tried first as a fix; the meter rejected it. The pin is
deliberately left floating in both images, and the reasoning plus both numbers are
recorded next to the pin in `zephyr/src/board_power.c`.

### 2.5 Resolved: the regulator's quiescent current

The LDO on this board is a **ME6217**, and its datasheet states its own
consumption outright:

> Low Current Consumption:
> During Operation: **100 µA (TYP.)**
> During Shutdown: 0.1 µA (TYP.)

The probe measured 99 µA with the CPU halted and every clock stopped. The only
load on the regulator was the MCU's System OFF current — about 0.4 µA, i.e.
nothing. A regulator with no load draws its quiescent current, so that reading is
essentially the regulator's own draw.

**The obvious next move was to remove it and see how much of the 99 µA it owned**,
with the board fed 3.6 V directly:

| Configuration | Reading |
| --- | --- |
| ME6217 in circuit, 3.3 V | ~99 µA |
| **ME6217 removed, 3.6 V direct** | **~40 µA** |

Two conclusions, and the second is a correction of the first draft of this
section:

**1. The regulator was a large contributor, and the budget becomes reachable.**
Sub-50 µA requires that component out of the path (or replaced — a ~1 µA low-Iq
part in its place projects to about 41 µA).

**2. "99 µA measured against 100 µA typical in the datasheet, agreeing to 1 %" was
a coincidence.** The part's real draw in this circuit is about **59 µA**, not the
datasheet's 100 µA *typ*; the remainder is something else. A datasheet *typical*
is a guide, not a guarantee — and a coincidence that flatters the hypothesis in
hand is the most dangerous kind, because it stops the search.

#### The correction that mattered: `VDDH`

An earlier version of this section dismissed the 3.6 V bench feed as "not a
product configuration", on the grounds that the nRF52840's VDD maximum is 3.6 V
while a charged LiPo is 4.2 V. That reasoning applies to **VDD**. It does not
apply to the node the board's battery actually reaches.

The battery path is a power-path switch — a P-MOSFET from `BAT` to **`VDDH`**,
with a 100 kΩ gate pull-down and a Schottky diode that takes over when USB is
present. `VDDH` is the nRF52840's **high-voltage input**: it accepts up to 5.5 V
and has the chip's own REG0 produce VDD from it.

| Pin | Range | LiPo 4.2 V |
| --- | --- | --- |
| `VDD` (normal voltage mode) | 1.7–3.6 V | over maximum |
| **`VDDH` (high voltage mode)** | **up to 5.5 V** | **inside it** |

So feeding the battery to `VDDH` is the mode that input exists for, and two
things follow:

* **The ME6217 may not be there to power the MCU at all.** If the chip runs from
  `VDDH`, that regulator's job is to make a 3.3 V rail for external modules — and
  it charges ~59 µA for the privilege. The SHT3x itself accepts 2.4–5.5 V and
  could run from the battery rail directly, which would make the rail
  unnecessary. That is a hardware decision, not a firmware one.
* **The datasheet's "System ON Idle ≈ 2.35 µA" does not describe this board.**
  That figure is for normal voltage mode. In high-voltage mode REG0 is in the
  path, and this project does not enable its DC/DC, so REG0 runs as an LDO.

### 2.6 The board's own sense divider: 18 µA, and it cannot be gated

With the regulator out of the path, the board still carries its own battery-sense
divider on P0.04, wired to the cell and always connected:

| | |
| --- | --- |
| resistors | two 100 kΩ, a 1:1 divider |
| permanently across the cell | 200 kΩ |
| current at 3.6 V | **18 µA** (21 µA at a full 4.2 V) |

Eighteen microamps is more than a third of the budget, spent on a reading taken
once every 30 s — and unlike a divider of our own it cannot be switched off: the
two ends are soldered to the cell and to ground with no pin in between.

That is why the firmware reads **P0.29** through an external 600 kΩ / 600 kΩ
pair instead, even though both dividers are 1:1 and the conversion is identical.
The external pair costs 3.0 µA — *and its top leg can be moved to a GPIO*, which
takes it to 0.017 µA. The accuracy argument is a wash; the difference is what can
still be changed later.

### 2.7 What is still unaccounted for

With the regulator out and the sense divider named, roughly **10–15 µA** is left
without an owner. Two variables changed at once in the regulator experiment
(regulator removed *and* supply raised from 3.3 V to 3.6 V, with the image on the
board not recorded), so the arithmetic does not close exactly.

The way to close it is the probe's two phases again, held at 3.6 V with the
regulator out: phase A against phase B gives the MCU's share, and whatever
survives phase B is board-level. It is recorded here as an open item rather than
rounded away.

---

## 3. Hypotheses that did not survive their control

| Hypothesis | How it died |
| --- | --- |
| "the floating LED pin is drawing it" | the probe read 99 µA with the pin floating **and** driven |
| "the datasheet's 100 µA typ explains the 99 µA" | removing the part showed it drew ~59 µA in this circuit |
| "the sense divider on P0.29 is the load" | P0.29's divider is not the one fitted; the board's own P0.04 pair is, and P0.29 reads clean |
| "the firmware is not sleeping properly" | phase B — CPU halted, every clock stopped — still drew 99 µA; the MCU's share is 4 µA |
| "RST held low is a free halted-CPU state" | it put the reset pull-up across the supply: 500 µA |
| "current falling as voltage rises is a board behaviour" | it was the meter's burden voltage |

## 4. Result

| Configuration | Standby current |
| --- | --- |
| firmware's own contribution (phase A − phase B) | **4 µA** |
| the same board as shipped (ME6217 in circuit) | ~99 µA |
| **ME6217 out of the path, 3.6 V direct** | **≤ 50 µA — meets the criterion** |

The firmware meets its side of the budget with a factor of twelve to spare. The
remaining budget is spent by a power component chosen before there was a budget
to meet — which is the usual way this target is missed, and the reason the
bare-board probe was worth building. It converts "the node draws 103 µA" into
"4 µA of application and ~99 µA of board": two different problems, with two
different fixes.

## 5. What to take away

1. **Measure, then explain, then claim.** Every wrong answer in §2.2 was
   plausible, and none of them came from a faulty part.
2. **A mechanism needs a control.** §2.3 is the case study: two builds, one
   difference, and the difference was not the cause.
3. **Beware the coincidence that agrees with you.** §2.5: 99 µA against a 100 µA
   datasheet *typical* read as confirmation and cost an evening.
4. **The measurement is part of the circuit.** A DMM's µA range, a USB cable, a
   held reset — all three changed the thing being measured.
5. **Attribute the residue.** The unexplained 10–15 µA is stated as unexplained
   rather than absorbed into a round number.
