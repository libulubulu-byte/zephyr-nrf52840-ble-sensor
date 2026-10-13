/*
 * Build-time tunables for the environmental sensor node.
 *
 * Kept as plain macros so the application has no extra Kconfig plumbing;
 * promote them to a <app>/Kconfig menu if the project grows more options.
 */

#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

/*
 * Cadence while a central is connected. The GATT notifications are driven by
 * a delayed work item, so this is exactly the notify interval.
 */
#define APP_NOTIFY_INTERVAL_S 2

/*
 * Cadence while no central is connected. Between samples the CPU stays in
 * System ON Idle and the kernel RTC keeps the time for us.
 */
#define APP_OFFLINE_SAMPLE_INTERVAL_S 30

/*
 * Advertising interval in 0.625 ms units while waiting for a reconnect.
 * 3200 == 2 s. A long interval is the single biggest lever on the average
 * standby current, so it is deliberately slow.
 *
 *     interval    advertising current at 0 dBm (approx)
 *        1 s      ~30 uA      the previous value of this build
 *        2 s      ~15 uA      what this build does
 *     100 ms      ~300 uA     ten times that, and the price of being found fast
 *
 * This was 1600 (1 s) and was doubled to 3200 to cut the radio's share of the
 * standby current, which a ~100 uA board reading made worth taking.
 *
 * A two-phase scheme - fast advertising for the first 60 s after boot and again
 * after every disconnect, then slow - was built here and then reverted, and the
 * reasoning is worth keeping, because the trade is not visible from either side
 * alone. It would make discovery feel immediate while costing almost nothing
 * over a day (300 uA for 60 s, then 30 uA, averages 30.2 uA). What it also does
 * is put a ~300 uA state into the firmware, so a standby reading taken inside
 * that window comes out ten times too high - and "why is my number wrong" has
 * already been the most expensive question in this project, repeatedly.
 *
 * The cost of a long steady interval is discovery latency, and the floor on this
 * hardware is known: field experience here found 3 s already too slow for a
 * phone to find or connect to the node. 2 s is halfway back from that limit and
 * was chosen with it in mind. If a phone starts failing to find the node, 1600
 * (1 s) is the value to put back - and note that the interval is not where the
 * large numbers are; see data/README.md.
 */
#define APP_ADV_INTERVAL_UNITS 3200

/* Give up (and stop advertising) after this many seconds, 0 = never. */
#define APP_ADV_TIMEOUT_S 0

/*
 * Battery voltage divider, Vbat = Vadc * NUM / DEN.
 *
 * Wiring: one of the two dividers this hardware offers, selected by
 * APP_BATT_SENSE_CHANNEL_IDX below against the order of the io-channels list
 * in boards/nordic/nrf52840_supermini/nrf52840_supermini.dts:
 *
 *   index 0   P0.04 / AIN2   the board's own pair, 100k + 100k, 1:1
 *   index 1   P0.29 / AIN5   an external pair on pad "029", 600k + 600k, 1:1
 *
 * Both are 1:1, so
 *
 *     Vadc = Vbat / 2    ->  NUM/DEN = 2/1
 *
 * and the ratio below is not a function of the choice. Only the current is,
 * which is the power paragraph.
 *
 * ---------------------------------------------------------------------------
 * The ratios this file has been through, in order, because the lesson is that
 * the number has to come off the board rather than off an assumption:
 *
 *   2/1   initial guess from the nice!nano reference design
 *   5/1   "corrected" from a 30k/7.5k pair - SMD markings 303 and 752 - that
 *         turned out not to be what is fitted
 *   2/1   the actual resistors: two 100k - the BOARD's pair
 *
 * Each wrong value produced a battery reading that looked plausible in the
 * log until it was checked against a multimeter. Check it against a meter.
 * ---------------------------------------------------------------------------
 *
 * Power: the divider is across the cell permanently and the firmware has no way
 * to switch it off, so its current is part of the standby floor. The *ratio*
 * and the *cost* are independent choices - the same 2:1 ratio built from
 * 30k/7.5k drew 101 uA and would have failed on its own. Resistor choice by
 * cost:
 *
 *     30k / 7.5k   101 uA    fails the budget outright
 *     100k / 100k   18 uA    the BOARD's pair on P0.04, always connected
 *     600k / 600k  3.0 uA    an external pair on P0.29
 *     gated        0.017 uA  that external pair with its top leg on a GPIO
 *
 * Eighteen microamps is more than a third of the whole 50 uA budget, spent on a
 * reading taken once every 30 s, and the board's divider draws it whether or
 * not anything reads it. So the choice between the two is not about what is
 * spent today - both dividers sit there drawing regardless - it is about what
 * can still be changed:
 *
 *   reading P0.04 exposes no extra cost, and lets an external pair come off,
 *   saving its 3.0 uA. It also forecloses the last row: the board's divider is
 *   soldered to the cell and to ground, with no pin in between to switch.
 *
 *   reading P0.29 keeps a divider whose own top leg can be put on a GPIO, so
 *   it is powered for the milliseconds the ADC samples and averages to
 *   nothing. That is worth about 3.0 uA now and ~21 uA if the board's divider
 *   is ever desoldered, at the cost of carrying a second divider meanwhile.
 *
 * Either answer is defensible; that is why it is a #define and not an argument.
 *
 * ---------------------------------------------------------------------------
 * Settling: 600k || 600k is a 300k source impedance, and the acquisition time
 * cannot be raised to compensate.
 *
 * nRF52840's SAADC TACQ is a 3-bit field offering only 3/5/10/15/20/40 us
 * (SAADC_CH_CONFIG_TACQ_Msk is 0x7, values 0..5). The board DTS already asks
 * for 40 us, so 40 us is the hardware ceiling - the driver's alternative
 * linear-TACQ path tops out at 750 ns, which would have rejected 40 us, so the
 * enum path is provably the one in use.
 *
 * If the reported vbat reads *low* and consistently by a percent or two, that
 * is the signature of the sampling capacitor not fully charging through 300k.
 * The fix is not software: put a 10 nF capacitor from the sense node to GND.
 * It gives the sample-and-hold a low-impedance source at the instant it
 * samples, has no effect on the DC ratio, and costs nothing at 3.2 uA. Dropping
 * back to 300k/300k (6.3 uA, 150k source) is the other option.
 *
 * The check that settles it is a multimeter on BAT+ against the reported value.
 * ---------------------------------------------------------------------------
 *
 * Also worth knowing: if the board has its own divider populated on P0.04, it
 * is still fitted and still drawing whatever it drew before. Adding this one on
 * top is only safe to assume once the standby current has actually been
 * measured.
 */
#define APP_BATT_ADC_ENABLE 1

/*
 * Which of the two sense inputs to read, by index into the io-channels list in
 * the board devicetree:
 *
 *   0   P0.04 / AIN2   the board's own divider
 *   1   P0.29 / AIN5   an external divider on pad "029"
 *
 * This is a one-line switch on purpose. The pin has been changed back and
 * forth several times, and every one of those changes was an edit to a
 * devicetree they should not have needed: both dividers exist on the hardware
 * regardless, both are 1:1, and the only thing that differs is which one the
 * ADC samples. Defining both channels and choosing here keeps the converter
 * configuration honest and makes the comparison a rebuild rather than a
 * rewrite.
 *
 * The cost difference is real and is documented in the block above - but note
 * that it is not a difference in what is *drawn*, only in what can still be
 * switched off later.
 */
#define APP_BATT_SENSE_CHANNEL_IDX 1

/* For the boot log, so the console states which input is in use rather than
 * leaving it to be worked out from a channel number.
 */
#if APP_BATT_SENSE_CHANNEL_IDX == 0
#define APP_BATT_SENSE_NAME "P0.04/AIN2 (board divider)"
#else
#define APP_BATT_SENSE_NAME "P0.29/AIN5 (external divider)"
#endif

#define APP_BATT_DIVIDER_NUM 2
#define APP_BATT_DIVIDER_DEN 1

/*
 * Number of ADC samples averaged per reading. This is software averaging, and
 * it is why the board DTS deliberately does not enable hardware oversampling:
 * nRF SAADC oversampling widens the raw value (12-bit + 4x oversampling gives
 * a 14-bit result) while adc_raw_to_millivolts() still divides by 2^12, which
 * makes every converted value 4x too high.
 */
#define APP_BATT_ADC_AVG 8

/*
 * Above this, the reading is a wiring fault rather than a battery - a LiPo
 * tops out around 4.2 V. The first run of this firmware reported 65501 mV
 * because the sense pin was sitting on the 3.3 V rail, and a saturated number
 * like that explains nothing. env_sensor.c now rejects it and says so.
 */
#define APP_BATT_PLAUSIBLE_MAX_MV 4400

/*
 * Bring-up aid: at boot, read all eight SAADC analog inputs and log the
 * millivolts on each.
 *
 * This exists because "which pad is my divider on" has cost this project
 * several rounds. The nRF52840 has eight analog inputs and they are all on port
 * 0, but which ones a given clone breaks out - and what its silkscreen calls
 * them - varies between the SuperMini, the Promicro and the nice!nano layouts.
 * Reading them all takes a few milliseconds, once, and turns a guess into a
 * reading.
 *
 * Set to 0 once the pin is confirmed. It is a diagnostic, not something to
 * ship, and it means nothing drains the divider between samples.
 */
#define APP_BATT_AIN_SWEEP_DIAG 1

/*
 * Status LED (blue, P0.15, active high).
 *
 * The heartbeat is off by default and that is a deliberate power decision:
 * a 3 ms blink of a ~1 mA LED once per second averages 3-10 uA, which is a
 * large slice of the <50 uA standby budget. Use "connected" mode instead -
 * it lights the LED only while a central is attached, when the radio already
 * dominates the power draw.
 */
#define APP_LED_HEARTBEAT_ENABLE 0
#define APP_LED_CONNECTED_ENABLE 1

/*
 * Shown in the boot banner so a log capture identifies the build.
 *
 * Bump this on every reflash you intend to verify. Without a version change
 * there is no way to tell from the console whether the image on the board is
 * the one you just built - which is exactly the confusion this line exists to
 * prevent.
 */
#define APP_VERSION_STRING "1.0.16"
#define APP_NCS_VERSION_STRING "v2.7.0"

#endif /* APP_CONFIG_H_ */
