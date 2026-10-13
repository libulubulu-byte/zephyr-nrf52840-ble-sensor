# Power measurement protocol

The `< 50 µA` criterion is measured, not estimated. `power_consumption.csv` is
the result sheet; this file is how to fill it. **The current values in the CSV
are empty on purpose** — do not paste numbers in until they come off a meter.

Everything in this file that is a lesson rather than a procedure is here because
it was learned by getting it wrong first. Both mistakes below produced numbers
that looked plausible and were not measurements of the node.

## Two rules, because both have already been broken

1. **Use a shunt, not the meter's µA range.**
2. **Power the board the way it actually runs** — not through the charger's
   input, and not with VBUS present.

### Rule 1: a shunt, not the µA range

A multimeter in its µA range puts a burden resistance between 1 kΩ and 10 kΩ in
series with the supply. At 400 µA through 1 kΩ the board never sees the voltage
on the dial — it sees 0.4 V less, and nothing in the reading says so.

That artefact is not hypothetical here. An early run dialled 3.3 V and 3.6 V and
read **400 µA and 150 µA** — a load whose current *falls* as voltage *rises*,
i.e. an effective resistance of −1.2 kΩ. No passive circuit does that. What
actually happened is that the measurement, not the board, changed.

Use a resistor and Ohm's law instead:

```
supply + ──[100 Ω]──┬── board VCC
                    │
              meter, mV range, across the 100 Ω
```

$$I = \frac{V_{\text{mV}}}{100\ \Omega}$$

| Current | Burden at 100 Ω | Effect on the board |
| --- | --- | --- |
| 400 µA | 40 mV | negligible |
| 100 µA | 10 mV | negligible |
| 2.35 µA | 0.24 mV | negligible |

**Also measure the voltage at the board's own pin while the shunt is in
circuit.** If it is not what the dial says, none of the other numbers mean
anything.

### Rule 2: power the board the way it actually runs

This board has a LiPo charger in the power path. Feeding it through the
charger's input (`5V` / `VBUS`) has two consequences that both show up in the
reading:

* **The charger is live.** Its quiescent current is in the measurement, and its
  input is what decides how much of it there is.
* **VBUS is present, so the USB peripheral is live** — and with the product
  firmware, the USB device stack initialises at boot
  (`CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y`).

The Arduino debugging notes kept with this board's previous project already
recorded what that costs:

> USB 一被初始化（CDC + 每 1ms SOF 中断）电流就被抬到几百 µA~mA，且 CPU 被
> SOF 每秒唤醒 1000 次，**永远测不到真实休眠电流**。

So: **power the `BAT` pad and leave the charger's input floating.** Then the
charger is inactive and the USB peripheral is dead by construction, and the
meter reads what the battery actually supplies — which is the number the
acceptance criterion is about.

### Rule 3: never hold RST to get a halted-CPU reading

Shorting the reset pad to GND looks like a free way to take the CPU out of the
picture. On this board it is not, and it produced a wrong conclusion before the
arithmetic was checked.

The reset pin has an internal pull-up, in the region of 10 kΩ on this part.
Holding the pin low puts that resistor straight across the supply, and the
current path runs through the meter:

```
supply + → meter → board VDD → internal pull-up → RST pad → your short → GND
```

That is ~400 µA at 3.3 V, and it accounted for a reading that had been taken as
evidence of a board-level load:

| Measurement | Reading |
| --- | --- |
| firmware idling | 102 µA |
| firmware idling, **RST held low** | **500 µA** |

102 µA plus the pull-up's ~400 µA is the 500 µA — the board did not change, the
rig did. To halt the CPU without touching the reset pin, use the probe's
**phase B (System OFF)**, below.

**Check which image is on the board before interpreting anything.** One command,
and it needs no extra hardware:

```powershell
powershell -File scripts\state.ps1
```

| Result | Meaning |
| --- | --- |
| **no `239A` device at all** | a probe image is running (USB is compiled out, so nothing enumerates) |
| `PID_8029` / a COM port | the product firmware is running |

A reading you cannot attribute to a known image is not a measurement. This is
not a formality: the two images differ by the Bluetooth stack, a USB peripheral
and a console.

## Step 0: the bare-board floor

Before trusting any number from the product firmware, measure the floor. If the
board leaks on its own, no amount of firmware tuning brings the product under
budget and the search belongs in hardware, not in `prj.conf`.

`power_test/` is a separate application. It walks through two states, marked by
**elapsed time only** — there is no console and, on this board, no LED either:

| When | State | Read the meter |
| --- | --- | --- |
| power-up | 4 blinks (if an LED is fitted), every GPIO parked | — |
| **t = 0..120 s** | **phase A — System ON Idle**, RAM retained, RTC running, 30 s cadence | **at t = 60 s** |
| t = 120 s | the chip drops to System OFF | — |
| **t = 120 s onward** | **phase B — System OFF**, CPU halted, clocks stopped, RST untouched | **at t = 180 s** |

Exit phase B with a reset or a power cycle; the board comes back up in phase A.

```bash
bash scripts/build_power_test.sh                        # build, in WSL
```

```powershell
powershell -File scripts\flash_wait.ps1 -Image firmware\power_test.uf2
```

An earlier version told the two states apart with LED blinks; the LED was then
removed from the board, which made the readings unattributable. Marking the
phases by the clock instead costs nothing and cannot go missing.

### Reading the result

Phase A is the number that compares against the product's standby state. Phase B
is what decides where to look next:

| Phase A | Phase B | Conclusion |
| --- | --- | --- |
| ~2-5 µA | ~0.5-2 µA | the board is fine; the current is in the firmware or the power path |
| ~20 µA | ~0.5-2 µA | a divider is populated and drawing — the sense divider on **P0.29** |
| **~100 µA** | **~0.5-2 µA** | **the chip is not idling** — same board, so the difference is the firmware's sleeper, not hardware |
| **~100 µA** | **~100 µA** | **the current is on the board.** The CPU and its clocks are halted in phase B, so firmware cannot be responsible |

The last two rows are the reason the probe has two states at all: the same
board is measured twice with only the chip's power state differing, so whatever
stays behind is hardware by elimination.

Two facts that shape the interpretation:

* **The 32.768 kHz crystal is in use** —
  `CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y` in both builds, and the product's
  30-second cadence has always kept time. So there is no RC-calibration
  overhead and the RTC costs ~0.3-0.5 µA.
* **`CONFIG_BOARD_ENABLE_DCDC` is off** in this project. Where a board
  populates the DC/DC inductors, enabling it takes 20-30 % off radio events.
  Turning it on where they are not populated can brown the part out, so check
  the board before flipping it.

## What a jumping reading means

A meter that alternates between two levels — observed here as roughly 100 µA
and 300 µA — is not measuring a resistive load. Something is switching, and the
two numbers are two states of it:

| What | Typical signature |
| --- | --- |
| a regulator in burst or PFM mode | jumps fast and irregularly; the average is the number that matters |
| a load duty-cycled by software | jumps at the software's period — correlate with the log |
| a charger in one of its modes | jumps as the charge cycle changes state |
| a supply in current-limit or hiccup | the reading moves with the supply setting, not with the board |

**The instantaneous reading is not the answer for any of these — the average
is.** A DMM's µA range cannot give that reliably, because it both burdens the
supply and integrates over an interval nobody chose. Use:

* **a shunt plus MIN/MAX/AVG** — three numbers instead of one, and the average
  is directly comparable against the 50 µA criterion;
* **a shunt plus an oscilloscope** — the best option: the burst *rate* names the
  culprit. Kilohertz means a switching regulator, ~1 kHz means USB SOF, hertz
  means something is being duty-cycled by software;
* **a bench supply with a long-integration current display**, if it has one.

Do not report a fluctuating reading as a single number. Report the min, the max
and the average, and say which instrument produced them.

## States to record

| State | Setup | Expected |
| --- | --- | --- |
| `standby_rtc` | no central connected, LED off, 30 s sampling cadence, advertising at 1 s | the acceptance target: **< 50 µA** |
| `standby_led` | same, but with `APP_LED_HEARTBEAT_ENABLE == 1` | compare — this is the measurement that justifies §6 of the README |
| `connected_2s` | central connected, 100 ms interval, notify every 2 s | hundreds of µA; the radio dominates |
| `offline_burst` | disconnected, taken during the ~200 ms around a sample | peak, not an average |

## How

1. Power the `BAT` pad; leave `5V`/`VBUS` floating. Shunt in the positive lead
   as shown above, meter on mV (or MIN/MAX/AVG).
2. Confirm which image is running (`scripts\state.ps1`) before writing anything
   down.
3. Let the firmware settle for at least one full 30 s sampling period, so the
   last command handling and the first sample are not in the window.
4. For `standby_rtc`, integrate over at least 10 advertising intervals (10 s);
   an instantaneous reading will catch an advertising event and read high.
5. Record the reading with an ISO-8601 timestamp in `power_consumption.csv`,
   along with the instrument and the supply point in the notes column.
6. Repeat each state 3 times and keep all rows. Spread is information: if the
   three `standby_rtc` rows disagree by more than ~20 %, something is periodic
   and unaccounted for.

## Observations so far

Recorded because each one cost a reflash to obtain. **The supply point was not
recorded for the first three, which is exactly why the protocol above now
demands it** — treat those as leads, not as results.

The narrative form of this section — which hypothesis was held, what killed it,
and what that cost — is [`../docs/DEBUG_NOTES.md`](../docs/DEBUG_NOTES.md). The
numbers stay here; the reasoning lives there.

### The one result that is a result: the MCU is not the problem

The probe's two phases were measured back to back on the same board, with the
only difference being the chip's power state:

| Phase | Chip state | Reading |
| --- | --- | --- |
| **A** (t = 60 s) | System ON Idle, RTC running, RAM retained | **103 µA** |
| **B** (t = 180 s) | **System OFF — CPU halted, all clocks stopped, RAM lost, RST untouched** | **99 µA** |

$$103\ \mu A - 99\ \mu A = \mathbf{4\ \mu A} \text{ for the MCU}$$

The nRF52840 datasheet puts System ON Idle with the RTC at about 2.35 µA, so
4 µA is the right order of magnitude: **the firmware is sleeping correctly** —
tickless kernel, LFXO, parked pins and all.

### What the remaining ~99 µA is: not the firmware

The phase A/B comparison says the current survives the CPU being halted and
every clock being stopped. Two readings on either side of a System OFF
transition, separated only by whether the chip is running, leaving a ~4 µA
difference, says the ~99 µA is not the application.

That much has held up. Everything that was built on top of it did not.

**A wrong attribution, recorded because the way it was reached is the useful
part.** A product build that left the LED pin disconnected measured 200 µA
where a build that drove it measured 103 µA. Two builds, one difference, 97 µA:
that looked like a mechanism, and it was written down as one — the LED pin,
floating, was the answer.

Then the control was asked. The bare-board probe parks every pin, so its LED
pin was already floating, and it measured **99 µA either way**:

| P0.15 in the probe | Reading |
| --- | --- |
| disconnected | 99 µA |
| driven low | 99 µA |

No effect. The 97 µA seen in the product did not reproduce, which means it was
never the pin — and a single product reading was not enough to name a mechanism.
The number of differences between two builds is one; the number of things
actually responsible is zero.

**What is established, across every combination tried:**

* 99 µA with the CPU idling, 99 µA with the CPU halted and every clock stopped
* 99 µA with P0.15 floating, 99 µA with it driven
* Bluetooth, USB, the console, sensors, ADC, SPI and flash all compiled out

It is a board-level load and no application-side setting has moved it.

**The one pin where measurement did produce a mechanism is P0.13**, the
charger's HICHG strap:

| P0.13 | Reading |
| --- | --- |
| floating | 102 µA |
| driven low | 600 µA |

A configuration input that swings the current by 500 µA belongs to a part that
is *running*. HICHG only means anything to a charger with an input, so the
charger is active — which points at the power path rather than at the chip, and
at the supply arrangement used to take the measurement.

### Resolved: the regulator's quiescent current

The LDO on this board is a **ME6217**, and its datasheet states its own
consumption outright:

> Low Current Consumption:
> During Operation: **100 µA (TYP.)**
> During Shutdown: 0.1 µA (TYP.)

The probe measured **99 µA** with the CPU halted and every clock stopped, so
the regulator's output load was the MCU's System OFF current — about 0.4 µA,
i.e. nothing. A regulator with no load draws its quiescent current, so that
reading is essentially the regulator's own draw.

**The obvious next move was to remove the regulator and see how much of the
99 µA it owned. It was done, with the board fed 3.6 V directly:**

| Configuration | Reading |
| --- | --- |
| ME6217 in circuit, 3.3 V | ~99 µA |
| **ME6217 removed, 3.6 V direct** | **~40 µA** |

Two conclusions, and the second is a correction of the first draft of this
section:

**1. The regulator was a large contributor, and the budget is reachable.**
40 µA is under the 50 µA criterion — the first configuration measured that is.
A low-quiescent-current regulator (of the order of 1 µA) in place of the ME6217
would put the node at roughly 41 µA.

**2. "99 µA measured against 100 µA in the datasheet, agreeing to 1 %" was a
coincidence, and it was written down here as agreement before being checked.**
The part's real draw in this circuit was about **59 µA**, not the 100 µA *typ*
on the datasheet; the remainder is something else. A datasheet *typical* is a
guide, not a guarantee, and a coincidence that flatters a hypothesis is the
most dangerous kind — it was the second time in this investigation that one
number was read as a mechanism.

**⚠️ Reading the schematic changed the meaning of that experiment, and this
paragraph was wrong the first time it was written.**

It said the 3.6 V feed was "a bench configuration, not a product one", on the
grounds that the nRF52840's VDD maximum is 3.6 V and a charged LiPo is 4.2 V.
That reasoning applies to the **VDD** pin. It does not apply to the node the
board's battery actually reaches.

The board's battery path is a power-path switch — a P-MOSFET from `BAT` to
**`VDDH`**, with a 100 kΩ gate pull-down and a Schottky diode that takes over
when USB is present. **`VDDH` is the nRF52840's high-voltage input**, which
accepts up to 5.5 V and has the chip's own REG0 produce VDD from it.

So feeding the battery to `VDDH` is not out of specification — it is the mode
that input exists for, and a charged LiPo sits comfortably inside it:

| Pin | Range | LiPo 4.2 V |
| --- | --- | --- |
| `VDD` (normal voltage mode) | 1.7-3.6 V | ✗ over maximum |
| **`VDDH` (high voltage mode)** | **up to 5.5 V** | **✓** |

Two things follow, and both change the picture:

* **The ME6217 may not be there to power the MCU at all.** If the chip runs
  from `VDDH`, that regulator's job is to make a 3.3 V rail for external
  modules — and it charges 59 µA for the privilege. The SHT3x itself accepts
  2.4-5.5 V and could run from the battery rail directly, which would make that
  3.3 V rail unnecessary. That is a hardware decision, not a firmware one.
* **The datasheet's "System ON Idle ≈ 2.35 µA" does not describe this board.**
  That figure is for normal voltage mode. In high-voltage mode the internal
  REG0 is in the path, and this project does not enable its DC/DC
  (`CONFIG_BOARD_ENABLE_DCDC` is unset), so REG0 runs as an LDO.

Test for the second point: `CONFIG_BOARD_ENABLE_DCDC=y`, and see whether the
remaining current moves. **Check that the board populates the DC/DC inductors
first** — enabling the switching regulator on a board without them can brown the
part out.

**What remains is ~35 µA, and one contributor now has a name.**

The board carries its own battery-sense divider on P0.04, wired to the cell and
always connected — it was there before any of this and it draws whether or not
the firmware reads it:

| | |
| --- | --- |
| resistors | two 100k, a 1:1 divider |
| permanently across the cell | 200k |
| current at 3.6 V | **18 µA** (21 µA at a full 4.2 V) |

Eighteen microamps is more than a third of the 50 µA budget, and unlike a
divider of our own it cannot be gated — the two ends are soldered to the cell
and to ground, with no pin in between to switch.

**Reading the board's divider instead of an external one was tried and
reverted.** Both are 1:1, so the conversion is identical and there is no
accuracy argument either way. The reason to prefer the external one is not that
it is cheaper today — it is that it can be changed: give its top leg a GPIO and
it is powered only while the ADC samples, which takes 3.0 µA down to 0.017 µA.
That option does not exist for a divider the board already soldered down.

So P0.04's divider keeps drawing its 18 µA and the application keeps ignoring
it. It is named here so that the number has an owner in the budget rather than
being an unexplained residue — and it is a candidate for removal if the last
18 µA is ever needed.

What that leaves unaccounted for, with the regulator out and the sense divider
named, is roughly 10-15 µA. Two variables changed at once in the regulator
experiment (regulator removed *and* supply raised from 3.3 V to 3.6 V, with the
firmware on the board not recorded), so the arithmetic does not close exactly
yet. The way to separate them is the probe's two phases, held at 3.6 V with the
regulator still out — phase A against phase B gives the MCU's share, and
whatever survives phase B is board-level.

The readings that had looked like board mysteries all reduce to measurement
artefacts:

| Reading | What it actually was |
| --- | --- |
| **500 µA with RST held low** | the reset pin's internal pull-up, shorted to GND through the meter (Rule 3) |
| **400 µA at 3.3 V, 150 µA at 3.6 V** — current falling as voltage rises | the meter's µA-range burden voltage, plus feeding a 3.3 V regulator from a 3.3 V supply, which per the same datasheet is below the headroom the part needs ($V_{IN} \geq V_{OUT} + 1.0\ V$) |
| **~100 ↔ 300 µA, jumping** | a load that is not resistive, read with an instrument that cannot average it |

### What this means for the acceptance criterion

**The criterion is met — ≤ 50 µA — with the ME6217 out of the path.** On this
board that is a hardware change rather than a firmware one: `< 50 µA` is not
achievable with the ME6217 in circuit, and no firmware change will make it so. It
becomes achievable as soon as that regulator is left out or replaced.

The full investigation — the measurement artefacts that produced four false
leads, and the one mechanism that was claimed and then retracted by its own
control — is in [`../docs/DEBUG_NOTES.md`](../docs/DEBUG_NOTES.md). This file
stays the procedure and the raw numbers.

| Configuration | Current |
| --- | --- |
| firmware's contribution, measured | **4 µA** — passes with a factor of twelve to spare |
| ME6217 in circuit | **~99 µA** ✗ |
| **ME6217 removed, 3.6 V direct (bench)** | **~40 µA** ✓ under the criterion |
| node with a low-Iq regulator substituted (~1 µA) | **~41 µA**, projected |

The firmware meets its side of the budget. The budget is spent by a power
component that was chosen before there was a budget to meet — which is the
usual way this target is missed, and the reason the bare-board probe was worth
building. It converts "the node draws 103 µA" into "4 µA of application and
100 µA of regulator": two different problems, with two different fixes.

The second configuration in that table is the one that matters for the
criterion, and it is the one place in this document where a number comes from a
hardware modification rather than from a shipped board. It is written down that
way on purpose: the node under a low-Iq regulator is a **projection supported by
a bench measurement**, not a reading taken from the deliverable.

Options, in the order they are worth considering:

1. **Replace the LDO with a low-quiescent-current part.** The part must match
   the footprint *and the pinout* — SOT-23-5 is a package, not a standard, and
   the pin arrangements of 5-pin regulators differ between families. Check the
   pinout before ordering. Note that the ME6217's own enable pin is no help
   here: it switches the regulator that supplies the MCU, so turning it off
   turns the application off with it.
2. **Do the acceptance demonstration on a board whose regulator suits the
   budget**, and say so in the README. The application is portable; the 100 µA
   is not part of the application.
3. **Record 100 µA as the floor and treat the criterion as unmet on this
   hardware.** Stating the limit and its cause is worth more than a number that
   was obtained with the regulator bypassed.

### Other observations

| Observation | Image | Supply | Note |
| --- | --- | --- | --- |
| ~102 µA | probe, P0.13 floating | not recorded | superseded by the phase A/B result above |
| ~600 µA | probe, P0.13 driven low | not recorded | see the HICHG note below |
| ~100 ↔ 300 µA, jumping | not confirmed | not recorded | see "What a jumping reading means" |
| **500 µA** | firmware idling, **RST held low** | not recorded | **not a board load** — the reset pull-up. See Rule 3 |
| **65 µA before the first connection, 51 µA after** | product firmware | battery | see below |

### A current that goes *down* after a connection

Product firmware, same supply, no other change: **65 µA before the node had ever
been connected to anything, 51 µA afterwards.** Connecting should cost current,
not save it, so this needed explaining rather than filing.

The application is not the explanation. `on_disconnected()` restores the offline
sampling cadence and restarts advertising on the same interval it used at boot,
and `power_set_connected()` only switches the cadence. Neither path changes
anything a meter would see.

What does change across a first connection is what the **controller** knows.
`CONFIG_BT_CTLR_XTAL_ADVANCED=y` is enabled, and the controller opens each radio
window early enough to cover the frequency error at both ends. Before the first
connection the only figure available for this end is the declared accuracy;
afterwards the peer has been measured. A narrower window means less radio and
less HFXO time, which is worth tens of microamps at a 1 s advertising interval.

**This is a hypothesis, and it is written down as one.** The configuration that
would confirm it is `CONFIG_CLOCK_CONTROL_NRF_K32SRC_30PPM=y` in `prj.conf`
(Zephyr's default is 50 ppm for a crystal source, while a common 32.768 kHz
tuning fork is specified at ±20 ppm). The test needs no connection at all:

| Cold reading after flashing | Conclusion |
| --- | --- |
| moves toward 51 µA | the declared accuracy was the limiter, and the fix works from boot |
| unchanged | the difference lies elsewhere; the setting is still right for the hardware, just not the explanation |

Too tight a declaration is not free — it narrows windows below what the crystal
can hold, and the sign of that is a link that will not stay up. If connections
start failing (0x3E) or disconnecting on supervision timeout (0x08), the value
goes back up. The crystal's datasheet is the correct answer; either number here
is a guess with a test attached.

### P0.13 / HICHG, and why the pin is left floating

P0.13 drives the LiPo charger's `HICHG` strap — the board's own notes say so:

> P0.13 是充电电流配置脚 HICHG，误接会干扰充电设置

Driving it low was tried first and the meter rejected it: **102 µA floating
against 600 µA driven low.** A strap that changes the current sixfold also
proves the charger is live, which is a statement about the power path rather
than about the pin.

So the pin is deliberately left disconnected, in both images, and the reasoning
plus both numbers are recorded in `zephyr/src/board_power.c`. This is a
measured decision, not a default — but it is also not a permanent one: a driven
pin is a stronger guarantee than a floating one, and the level should be pinned
down by measurement before this is called finished.

## If `standby_rtc` is above 50 µA

**Read the breakdown first, because the obvious lever is not the biggest one.**

A node at 1 s advertising spends roughly 30 µA on being findable, on top of
whatever the board costs. That 30 µA is not waste — it is the feature — and
trading it away is a deliberate choice rather than a free one:

> **The cost of lengthening the interval is discovery latency, and the floor on
> this hardware is known.** Brainstorming at a desk says 1 s → 2 s halves the
> radio's share; the arithmetic is right but the price is not free. The previous
> project on this hardware found that **3 s was already enough for a phone to
> fail to find or connect to the node**, so the interval was pinned at 1 s at the
> time. Field measurement beats arithmetic here.

**The interval is now 2 s.** It was doubled from `1600` to `3200` units to cut the
radio's share against a ~100 µA reading on the shipped board, choosing the
midpoint between the 1 s that was reliable and the 3 s that was not. If discovery
starts failing, `1600` is the value to put back.

That notwithstanding, the board floor is still where the large numbers are, so
attack it first and treat the interval as the last lever:

| Lever | Typical saving | Where the evidence is |
| --- | --- | --- |
| **The always-on sense divider** (P0.04, board-fitted, two 100 kΩ) | **18 µA** | measured; `data/README.md`, and the DTS comment |
| **Whatever else the board draws** — the residue after the divider and the MCU | ~11 µA on this board | phase A against phase B measured 4 µA for the MCU, so this is board-level |
| **A redundant external divider**, if one is fitted as well | 3.0 µA | this project's original P0.29 pair |

Only after those are gone do the radio-side levers become worth touching:

1. **Enable DC/DC** — `CONFIG_BOARD_ENABLE_DCDC=y`. Typically 20-30 % off the
   radio events. Only safe if this board batch actually populates the DC/DC
   inductors; confirm the board still boots before trusting the number.
2. **Lower TX power.** Default is 0 dBm; `-8 dBm` cuts the peak of every
   advertising event at the cost of range.
3. **Check for a leaked drive.** A pin left as `GPIO_OUTPUT_HIGH` into an
   external pull-down, or an unconfigured floating input, can quietly cost tens
   of µA.

If the number is still high after all of those, the measurement is no longer
describing the firmware — and the probe's phase A/B comparison is what proves
it. Two tests then identify the culprit without desoldering anything or
reflashing.

### Test A: is a resistor doing it, and on which rail?

With the board **unpowered**, measure resistance from each supply node to GND.

The arithmetic to compare against:

$$\frac{3.3\ V}{99\ \mu A} = 33\ k\Omega$$

| Measure | **≈ 33 kΩ means** |
| --- | --- |
| `3V3` → `GND` | a load sitting on the 3V3 rail |
| **`BAT` → `GND`** | **a permanently populated divider or load on the battery node — the usual culprit on this class of board, and the usual reason a clone draws current no firmware can stop** |
| `5V` → `GND` | a load on the regulator's input |

| much higher (>1 MΩ) on all three | an active load. A regulator's quiescent current and a charger's operating current are not resistances and will not show up |

**Start with `BAT`.** A sense divider wired to the battery node is powered
whenever the battery is, which is exactly the behaviour the probe observed: the
current did not move between phase A and phase B, because a resistor does not
care what the CPU is doing.

**A pull-up on its own is never the answer.** A pull-up draws current only when
something pulls its far end low. With every GPIO parked as an input, no internal
pull is enabled, so a lone pull-up has no path to anywhere. If a resistor is
carrying 99 µA, its other end is connected to ground somewhere *on the board* —
that is a divider, or a resistor in series with something that sinks. The reset
pin showed the same mechanism from the other side: it only drew current once it
was shorted to GND.

**Cheapest check of all:** if that 33 kΩ is a resistor feeding an LED, the LED
will be faintly lit. 99 µA through an LED is dim but visible in a dark room —
cup your hands around the board and look. This can identify a hardware-managed
charge LED in seconds, without a meter and without desoldering anything.

### Test B: regulator, or load on its output?

Power the board at the **`3V3` pad** instead of `BAT` or `5V`/`VBUS`, which
takes the on-board regulator out of the regulation path.

| Reading | Meaning |
| --- | --- |
| collapses to a few µA | the **regulator's own quiescent current**. A cheap LDO's Iq is commonly 50-150 µA, which brackets the reading |
| unchanged | the load hangs on the 3V3 rail and has nothing to do with the regulator |

### The suspect list

In rough order of likelihood on this board:

| Suspect | How to test |
| --- | --- |
| the sense divider on P0.29 | Test A. At 3.3 V, 99 µA is exactly 33 kΩ across the rail — check what was fitted |
| the on-board regulator's quiescent current | Test B. A cheap LDO's Iq is often 50-150 µA |
| the LiPo charger | only active with an input present — removing the supply from `5V`/`VBUS` takes it out |
| the `~CHG` charge LED on P0.17 | hardware-managed, the firmware cannot turn it off; look at it |
| a board LED sitting lit | see the next section |
| the charger's `HICHG` strap (P0.13) | floating measured better than driven low; see above |

### If it turns out to be the sense divider, the fix is to gate it

A battery-sense divider must not sit across the rail permanently. Drive its top
from a GPIO and power it only for the few milliseconds the ADC needs:

$$100\ \mu A \times \frac{5\ ms}{30\ s} \approx \mathbf{0.017\ \mu A} \text{ average}$$

That is free, and it is a real product-level fix: a divider left connected for
the convenience of one reading per 30 s is one of the classic ways these boards
miss a microamp budget.

## Why the LED matters so much

The LED is the single easiest way to fail this test. Worst case (steady on) it is
~1 mA — **20× the entire budget**. Even a 3 ms blink once per second averages
3-10 µA, which is 6-20 % of the budget for a feature nobody is watching during a
measurement. That is why `APP_LED_HEARTBEAT_ENABLE` defaults to `0` and the
`standby_led` row exists: it turns an opinion into a number.

One caveat specific to this board: the status LED in the board definition is
declared `GPIO_ACTIVE_HIGH`, taken from the Adafruit bootloader's
`nice_nano/board.h`. The board's own notes describe *other* LEDs on it
(P0.06 / P0.26 / P0.30) as common-anode, i.e. **lit when driven low**. The
firmware never touches those, so they sit at their reset state and stay off —
but if any LED is visibly lit with no central connected, this is where to look,
and it is worth ~200-400 µA.
