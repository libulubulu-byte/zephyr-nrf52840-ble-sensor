/*
 * Bare-board current probe.
 *
 * This application exists to answer one question before any further power work
 * on the real node is worth doing: what does the board itself draw, with no
 * Bluetooth, no USB, no console, no sensors, no flash access and nothing to
 * wake up for beyond the RTC the product uses?
 *
 * ===========================================================================
 *  THE MEASUREMENT: TWO PHASES, MARKED BY THE CLOCK AND NOTHING ELSE
 *
 *     t = 0            release anything the bootloader left on, 4 blinks,
 *                      park every GPIO
 *     t = 0..120 s     PHASE A - System ON Idle, 30 s RTC cadence
 *                      -> read the meter at t = 60 s
 *     t = 120 s        one 30 s period elapses and then the chip drops to
 *                      System OFF
 *     t = 120 s..      PHASE B - System OFF, CPU halted, RAM lost
 *                      -> read the meter at t = 180 s or later
 *
 * Exit is by reset or power cycle. The board comes back up in phase A.
 *
 * Phase A is the number that compares against the product's standby state:
 * the same power state, the same RTC, with every subsystem removed. Phase B is
 * the silicon floor - the nRF52840 datasheet puts System OFF around 0.4 uA -
 * and it is what separates the two possible worlds:
 *
 *     phase B ~0.5-2 uA   the chip really is idling in phase A, so the extra
 *                         ~100 uA is on the board. Go to hardware.
 *     phase B ~100 uA     the chip is not the difference at all, and whatever
 *                         draws the current draws it with the CPU halted and
 *                         every clock except the 32.768 kHz source stopped.
 *
 * ===========================================================================
 *  DO NOT USE "HOLD RST" TO GET A HALTED-CPU READING ON THIS BOARD
 *
 * An earlier instruction said to short RST to GND and hold it, to take the CPU
 * out of the picture. That produced 500 uA where the running firmware produced
 * 102 uA, which looked like a board-level load until the arithmetic was done.
 *
 * The reset pin has an internal pull-up, in the region of 10 kohm on this
 * part. Holding the pin low puts that resistor straight across the supply:
 *
 *     supply + -> meter -> board VDD -> pull-up -> RST pad -> your short -> GND
 *
 * Every microamp of it flows through the meter. At ~3.3 V and ~8 kohm that is
 * about 400 uA, which is the whole difference between the two readings:
 *
 *     102 uA (firmware idling) + ~400 uA (reset pull-up) = ~500 uA  - measured
 *
 * So the 500 uA was never the board. It was the measurement rig. Phase B is how
 * to ask the same question without touching the reset pin: System OFF halts the
 * CPU and stops the clocks while leaving RST alone.
 *
 * ===========================================================================
 *  WHAT HAS ALREADY BEEN RULED OUT
 *
 *   the firmware        BT, USB, serial, console, sensor, ADC, SPI and flash
 *                       are all compiled out of this image
 *   an inherited USB    the Adafruit UF2 bootloader is itself a USB device, so
 *    clock              it enables USBD and the 48 MHz clock derived from HFXO.
 *                       release_inherited_usb() below stops both. The reading
 *                       did not move, so that was not it - a clean negative,
 *                       and the teardown stays because it is correct.
 *   GPIO states         park_pins() leaves every pin as a reset leaves it, so
 *                       the firmware's pin states and the reset pin states are
 *                       the same thing
 *
 * What that leaves is either ~100 uA of board hardware that is there whatever
 * the chip does, or a chip that is not actually idling. Phase B tells them
 * apart, and it is the only remaining reason this image has two states.
 * ===========================================================================
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <hal/nrf_clock.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>
#include <hal/nrf_usbd.h>

/* How long phase A holds before the chip drops to the floor. */
#define PHASE_A_SECONDS 120

/* The product's offline sampling cadence; kept so the RTC stays awake. */
#define SAMPLE_PERIOD_SECONDS 30

#define BLINK_ON_MS 60
#define BLINK_OFF_MS 120

/*
 * Pins that are wired to something on the board, and therefore must be driven
 * to a defined level rather than parked with the rest.
 *
 * The distinction park_pins() gets wrong if you let it: Nordic's advice to
 * leave unused pins disconnected is about pins with no trace on them. A pin
 * with a trace is not unused, and disconnecting it hands a gate, an enable or
 * a strapping input an undefined level.
 *
 * Both of these were settled by measurement, and they wanted opposite answers,
 * which is the point - the rule is not "disconnect everything" and not "drive
 * everything low", it is "find out what each pin's circuit needs".
 */
#define BOARD_PIN_LED           NRF_GPIO_PIN_MAP(0, 15)
#define BOARD_PIN_CHARGER_HICHG NRF_GPIO_PIN_MAP(0, 13)

#if DT_NODE_EXISTS(DT_ALIAS(led0))
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define LED_PRESENT 1
#else
#define LED_PRESENT 0
#endif

/*
 * Release the USB peripheral and the high-frequency clock, in case the
 * bootloader left them running.
 *
 * The Adafruit UF2 bootloader is itself a USB device - that is how a .uf2 gets
 * in - so it enables the USBD peripheral and the 48 MHz clock derived from
 * HFXO, because USB Full-Speed cannot run from anything else. If it hands over
 * without tearing that down, the application starts with a crystal already
 * oscillating, and no Kconfig in the application can switch off a clock it
 * never asked for:
 *
 *     LF clock (LFXO), 32.768 kHz   ->  ~0.3-0.5 uA
 *     HF clock (HFXO), 32 MHz       ->  hundreds of uA
 *
 * On this board that turned out not to be the cause - the reading did not move
 * when this was added - but it stays: it costs nothing, it removes a variable,
 * and it is the correct thing for an application that wants no USB at all.
 *
 * Only two registers, and the restraint is deliberate. An earlier version also
 * masked the USB interrupts over in POWER, which was taken back out for two
 * reasons: it saved nothing, because a detector in an unclocked peripheral is
 * not a power saving; and POWER is the one peripheral here whose registers are
 * not entirely volatile across a reset - GPREGRET and RESETREAS are retained
 * on purpose, because that is how the bootloader finds out why it was reset.
 * Register state in the block the bootloader reads is not a place for a power
 * tweak to leave fingerprints.
 *
 * Both writes that remain are in peripherals a reset clears outright, so
 * nothing here reaches past this boot: the bootloader runs first next time,
 * out of flash it owns, and initialises USBD and the HFXO itself.
 */
static void release_inherited_usb(void)
{
	nrf_usbd_disable(NRF_USBD);
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTOP);
}

/*
 * Four blinks at power-on, purely as a "the probe is running" marker for
 * boards that still have their LED. Nothing here depends on it.
 */
static void blink(int times)
{
#if LED_PRESENT
	for (int i = 0; i < times; i++) {
		gpio_pin_set_dt(&led, 1);
		k_sleep(K_MSEC(BLINK_ON_MS));
		gpio_pin_set_dt(&led, 0);
		k_sleep(K_MSEC(BLINK_OFF_MS));
	}
#else
	ARG_UNUSED(times);
#endif
}

/*
 * Put every GPIO back into the state a reset leaves it in: input, no pull,
 * input buffer disconnected.
 *
 * This is not cosmetic. A pin left as an input with its buffer connected draws
 * current whenever it sits between the rails, and a pin left driving against an
 * external pull-up draws a great deal more. Nordic's power guidance is explicit
 * that unused pins should be left disconnected.
 *
 * Two pins are deliberately skipped: P0.00/P0.01, which are the 32.768 kHz
 * crystal pins on boards that fit one - disturbing them would upset the
 * low-frequency clock - and P0.18, which is nRESET.
 *
 * P0.13 and P0.15 are *not* skipped here - they are parked with everything
 * else and then driven by hold_board_pins() below. Parking is the right
 * starting point; leaving them parked is not, and that mistake measured 99 uA.
 */
static void park_pins(void)
{
	for (uint8_t port = 0; port < 2; port++) {
		/* Port 0 has 32 pins on the nRF52840, port 1 has 16. */
		uint8_t count = (port == 0) ? 32 : 16;

		for (uint8_t pin = 0; pin < count; pin++) {
			if (port == 0 && (pin == 0 || pin == 1 || pin == 18)) {
				continue;
			}

			nrf_gpio_cfg_default(NRF_GPIO_PIN_MAP(port, pin));
		}
	}
}

/*
 * Drive the board's wired pins to the level their circuit needs.
 *
 * This is the fix for the 99 uA that made this probe useless for a week. The
 * probe was measuring ~102 uA with every subsystem compiled out and the CPU
 * either idling or halted, and the number did not move between phase A and
 * phase B - which is what a board-level load looks like. It was not a board
 * level load. It was P0.15, left floating by park_pins().
 *
 * What this function does NOT fix, and was believed to fix for one build:
 *
 *   P0.15   floating          99 uA
 *           driven low        99 uA      <- no change at all
 *
 * The 99 uA was attributed to this pin after a product build measured 200 uA
 * where another had measured 103, and the only difference between those two
 * builds was whether the LED pin was driven or left disconnected. That reading
 * did not reproduce here: the probe parks every pin, so P0.15 is floating in
 * the version of this function that had no hold_board_pins() at all, and it
 * still measured 99 uA. Driving it low changes nothing.
 *
 * So the attribution was wrong, and the lesson is about method rather than
 * about this pin: a single reading from the product justified a conclusion that
 * the control was never asked to confirm. The control has now been asked, and
 * it disagrees. Two pins whose levels differ, one number, is not a mechanism.
 *
 * P0.15 is still driven low below, because it is the state the product firmware
 * puts it in and the two images should agree about the board. It is not
 * claimed to save anything.
 *
 * What the probe does establish, across every combination tried: 99 uA with the
 * CPU idling, 99 uA with the CPU halted and every clock stopped, 99 uA with
 * P0.15 floating and 99 uA with it driven. It is a board-level load, and no
 * application-side setting has moved it.
 *
 * P0.13 is the pin where the measurement did produce a mechanism: the charger's
 * HICHG strap, 102 uA floating against 600 uA driven low. A configuration pin
 * that swings the current by 500 uA is a configuration pin on a part that is
 * running - which is the strongest evidence available that the charger is
 * active.
 */
static void hold_board_pins(void)
{
	nrf_gpio_cfg_output(BOARD_PIN_LED);
	nrf_gpio_pin_clear(BOARD_PIN_LED);

	nrf_gpio_cfg_default(BOARD_PIN_CHARGER_HICHG);
}

int main(void)
{
#if LED_PRESENT
	if (device_is_ready(led.port)) {
		(void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}
#endif

	/*
	 * First, before the blinks even: whatever the bootloader left running
	 * belongs to the bootloader, not to this image.
	 */
	release_inherited_usb();

	blink(4);

	/*
	 * Park everything before settling, so the I2C pull-ups, any divider, the
	 * LED pads and the charger's HICHG strap are not being driven by us. On a
	 * board with an LED still fitted this also leaves it off for good - the
	 * marker has been given, and a lit LED would swamp the reading.
	 */
	park_pins();
	hold_board_pins();

	/*
	 * PHASE A - System ON Idle with nothing running but the RTC.
	 *
	 * k_sleep() re-arms the same timeout every 30 s so the power state does
	 * not drift, and so the RTC really is the only thing alive. Read the
	 * meter at t = 60 s.
	 */
	for (int elapsed = 0; elapsed < PHASE_A_SECONDS;
	     elapsed += SAMPLE_PERIOD_SECONDS) {
		k_sleep(K_SECONDS(SAMPLE_PERIOD_SECONDS));
	}

	/*
	 * PHASE B - System OFF.
	 *
	 * Written straight to the POWER peripheral rather than through
	 * sys_poweroff(): the power management subsystem's POWEROFF symbol
	 * depends on CONFIG_PM, and nRF52 SoCs select HAS_POWEROFF but never
	 * HAS_PM - there are no intermediate power states to program on this
	 * part. Going to the register is both shorter and honest about what is
	 * happening.
	 *
	 * The CPU is halted and every clock stops. RST is not touched, which is
	 * the entire point: this is the halted-CPU reading that holding the
	 * reset pin cannot give, because the reset pull-up adds its own current
	 * to the meter.
	 *
	 * Does not return. Exit with a reset or a power cycle.
	 */
	nrf_power_system_off(NRF_POWER);

	CODE_UNREACHABLE;
}
